/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// glsl_to_usf — offline conversion tool that consumes the JSONL + .glsl dump produced by the
// Phase A/B runtime ShaderDumpSink and emits one `.vs.usf` + `.ps.usf` per unique program plus a
// single `manifest.json` listing register bindings for the UE RHI adaptation layer.
//
// Usage:
//   ./glsl_to_usf --input <dump-dir> --output <usf-dir>
//
// Expected layout of <dump-dir>:
//   <dump-dir>/programs.jsonl
//   <dump-dir>/shaders/<keyHex>.vert.glsl
//   <dump-dir>/shaders/<keyHex>.frag.glsl
//
// Output layout:
//   <usf-dir>/hlsl/<keyHex>.vs.usf
//   <usf-dir>/hlsl/<keyHex>.ps.usf
//   <usf-dir>/manifest.json

#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "GlslRectNormalizer.h"
#include "GlslToSpv.h"
#include "ManifestWriter.h"
#include "SpvToHlsl.h"
#include "UsfPackager.h"

namespace tgfx {
namespace {

// --- Minimal JSONL parser for the exact shape emitted by ShaderDumpSink ---------------------
//
// We deliberately avoid pulling in a full JSON parser: the dump schema is fixed and simple, so
// field extraction via substring scanning is sufficient here. If the schema grows, replace this
// with the existing tools/shader_variant_dump/JsonWriter helpers.

struct JsonlEntry {
  std::string keyHex;
  std::string vsFile;
  std::string fsFile;
  std::string geometryProcessor;
  std::vector<std::string> fragmentProcessors;
  size_t numColorProcessors = 0;
  std::string xferProcessor;
  // Number of vertex-rate + instance-rate attributes observed in the layout block; used to seed
  // the HLSL ATTRIBUTEN semantic remap.
  size_t attributeCount = 0;
};

// Extract the string value of a `"key":"value"` pair starting from a known offset. Returns empty
// string on failure.
std::string ExtractStringField(const std::string& line, const std::string& key, size_t from = 0) {
  auto needle = "\"" + key + "\":\"";
  auto start = line.find(needle, from);
  if (start == std::string::npos) return "";
  start += needle.size();
  auto end = line.find('"', start);
  if (end == std::string::npos) return "";
  return line.substr(start, end - start);
}

size_t ExtractNumberField(const std::string& line, const std::string& key) {
  auto needle = "\"" + key + "\":";
  auto start = line.find(needle);
  if (start == std::string::npos) return 0;
  start += needle.size();
  size_t end = start;
  while (end < line.size() && (std::isdigit(static_cast<unsigned char>(line[end])) != 0)) {
    ++end;
  }
  if (end == start) return 0;
  return static_cast<size_t>(std::stoul(line.substr(start, end - start)));
}

// Count occurrences of a substring (used to count attribute entries in the layout block).
size_t CountOccurrences(const std::string& haystack, const std::string& needle) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

bool ParseJsonlLine(const std::string& line, JsonlEntry& entry) {
  entry.keyHex = ExtractStringField(line, "keyHex");
  entry.vsFile = ExtractStringField(line, "vsFile");
  entry.fsFile = ExtractStringField(line, "fsFile");
  entry.geometryProcessor = ExtractStringField(line, "geometryProcessor");
  entry.xferProcessor = ExtractStringField(line, "xferProcessor");
  entry.numColorProcessors = ExtractNumberField(line, "numColorProcessors");

  // FragmentProcessors is an array of strings; extract between the two square brackets that
  // follow the `"fragmentProcessors":` token, then split on `","`.
  auto fpStart = line.find("\"fragmentProcessors\":[");
  if (fpStart != std::string::npos) {
    fpStart += std::strlen("\"fragmentProcessors\":[");
    auto fpEnd = line.find(']', fpStart);
    if (fpEnd != std::string::npos) {
      auto inner = line.substr(fpStart, fpEnd - fpStart);
      size_t cursor = 0;
      while (cursor < inner.size()) {
        auto open = inner.find('"', cursor);
        if (open == std::string::npos) break;
        auto close = inner.find('"', open + 1);
        if (close == std::string::npos) break;
        entry.fragmentProcessors.push_back(inner.substr(open + 1, close - open - 1));
        cursor = close + 1;
      }
    }
  }

  // Count the `stepMode` field occurrences inside the attributes array to obtain attribute
  // count without fully parsing nested structures.
  auto attrsStart = line.find("\"attributes\":[");
  auto attrsEnd = attrsStart == std::string::npos ? std::string::npos : line.find(']', attrsStart);
  if (attrsStart != std::string::npos && attrsEnd != std::string::npos) {
    auto slice = line.substr(attrsStart, attrsEnd - attrsStart);
    entry.attributeCount = CountOccurrences(slice, "\"stepMode\"");
  }

  return !entry.keyHex.empty() && !entry.vsFile.empty() && !entry.fsFile.empty();
}

// --- Filesystem helpers ---------------------------------------------------------------------

bool ReadFile(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool WriteFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << content;
  return true;
}

void EnsureDir(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    mkdir(path.c_str(), 0755);
  }
}

// --- Conversion driver ----------------------------------------------------------------------

bool ConvertOneProgram(const std::string& inputDir, const std::string& outputDir,
                       const JsonlEntry& entry, ManifestWriter& manifest, std::string& errorOut,
                       bool& skipped) {
  skipped = false;
  std::string vsGlsl;
  std::string fsGlsl;
  if (!ReadFile(inputDir + "/" + entry.vsFile, vsGlsl)) {
    errorOut = "failed to read " + entry.vsFile;
    return false;
  }
  if (!ReadFile(inputDir + "/" + entry.fsFile, fsGlsl)) {
    errorOut = "failed to read " + entry.fsFile;
    return false;
  }

  // Phase D: rewrite sampler2DRect uniforms to sampler2D + inverse-size companion uniforms so
  // SPIRV-Cross's HLSL backend (which does not support the SampledRect capability) accepts the
  // shader. Macro-based rect sampling (GaussianBlur1D's TGFX_GB1D_SAMPLE) and helper call-site
  // coord wrapping are handled inside the normalizer. Structural surprises surface as errors.
  auto fsNorm = NormalizeRectSamplers(fsGlsl, GlslStage::Fragment);
  if (!fsNorm.errorMessage.empty()) {
    errorOut = "FS rect-normalize: " + fsNorm.errorMessage;
    return false;
  }
  fsGlsl = std::move(fsNorm.glsl);
  auto vsNorm = NormalizeRectSamplers(vsGlsl, GlslStage::Vertex);
  if (!vsNorm.errorMessage.empty()) {
    errorOut = "VS rect-normalize: " + vsNorm.errorMessage;
    return false;
  }
  vsGlsl = std::move(vsNorm.glsl);

  auto vsSpv = CompileGlslToSpv(vsGlsl, GlslStage::Vertex);
  if (!vsSpv.errorMessage.empty()) {
    errorOut = "VS GLSL->SPV: " + vsSpv.errorMessage;
    return false;
  }
  auto fsSpv = CompileGlslToSpv(fsGlsl, GlslStage::Fragment);
  if (!fsSpv.errorMessage.empty()) {
    errorOut = "FS GLSL->SPV: " + fsSpv.errorMessage;
    return false;
  }

  auto vsHlsl =
      ConvertSpvToHlsl(vsSpv.spirv, GlslStage::Vertex, static_cast<uint32_t>(entry.attributeCount));
  if (!vsHlsl.errorMessage.empty()) {
    errorOut = "VS SPV->HLSL: " + vsHlsl.errorMessage;
    return false;
  }
  auto fsHlsl = ConvertSpvToHlsl(fsSpv.spirv, GlslStage::Fragment, 0);
  if (!fsHlsl.errorMessage.empty()) {
    errorOut = "FS SPV->HLSL: " + fsHlsl.errorMessage;
    return false;
  }

  auto vsUsf = PackageAsUsf(vsHlsl.hlsl, GlslStage::Vertex, entry.keyHex);
  auto fsUsf = PackageAsUsf(fsHlsl.hlsl, GlslStage::Fragment, entry.keyHex);

  auto vsRel = "hlsl/" + entry.keyHex + ".vs.usf";
  auto fsRel = "hlsl/" + entry.keyHex + ".ps.usf";
  if (!WriteFile(outputDir + "/" + vsRel, vsUsf)) {
    errorOut = "failed to write " + vsRel;
    return false;
  }
  if (!WriteFile(outputDir + "/" + fsRel, fsUsf)) {
    errorOut = "failed to write " + fsRel;
    return false;
  }

  ManifestEntry me;
  me.keyHex = entry.keyHex;
  me.vsFile = vsRel;
  me.psFile = fsRel;
  me.geometryProcessor = entry.geometryProcessor;
  me.fragmentProcessors = entry.fragmentProcessors;
  me.numColorProcessors = entry.numColorProcessors;
  me.xferProcessor = entry.xferProcessor;
  me.cbuffers = vsHlsl.cbvBindings;
  me.cbuffers.insert(me.cbuffers.end(), fsHlsl.cbvBindings.begin(), fsHlsl.cbvBindings.end());
  me.srvs = vsHlsl.srvBindings;
  me.srvs.insert(me.srvs.end(), fsHlsl.srvBindings.begin(), fsHlsl.srvBindings.end());
  me.samplers = vsHlsl.samplerBindings;
  me.samplers.insert(me.samplers.end(), fsHlsl.samplerBindings.begin(),
                     fsHlsl.samplerBindings.end());
  me.attributes = vsHlsl.attributes;
  me.rectSamplers = fsNorm.rects;
  me.rectSamplers.insert(me.rectSamplers.end(), vsNorm.rects.begin(), vsNorm.rects.end());
  manifest.appendEntry(me);
  return true;
}

void PrintUsage(const char* prog) {
  std::cerr << "Usage: " << prog << " --input <dump-dir> --output <usf-dir>\n"
            << "  <dump-dir>  Directory produced by TGFX_SHADER_DUMP_DIR=... (contains\n"
            << "              programs.jsonl and shaders/<keyHex>.{vert,frag}.glsl).\n"
            << "  <usf-dir>   Destination for hlsl/*.usf and manifest.json.\n";
}

}  // namespace
}  // namespace tgfx

int main(int argc, char** argv) {
  std::string inputDir;
  std::string outputDir;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      inputDir = argv[++i];
    } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
      outputDir = argv[++i];
    } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      tgfx::PrintUsage(argv[0]);
      return 0;
    }
  }
  if (inputDir.empty() || outputDir.empty()) {
    tgfx::PrintUsage(argv[0]);
    return 1;
  }

  tgfx::EnsureDir(outputDir);
  tgfx::EnsureDir(outputDir + "/hlsl");

  std::ifstream jsonl(inputDir + "/programs.jsonl");
  if (!jsonl) {
    std::cerr << "Failed to open " << inputDir << "/programs.jsonl\n";
    return 2;
  }
  std::ofstream manifestOut(outputDir + "/manifest.json");
  if (!manifestOut) {
    std::cerr << "Failed to open " << outputDir << "/manifest.json for writing\n";
    return 3;
  }
  tgfx::ManifestWriter manifest(manifestOut);
  manifest.beginManifest();

  size_t ok = 0;
  size_t skipped = 0;
  size_t failed = 0;
  std::string line;
  while (std::getline(jsonl, line)) {
    if (line.empty()) continue;
    tgfx::JsonlEntry entry;
    if (!tgfx::ParseJsonlLine(line, entry)) {
      std::cerr << "Skipping malformed JSONL line\n";
      ++failed;
      continue;
    }
    std::string err;
    bool wasSkipped = false;
    if (tgfx::ConvertOneProgram(inputDir, outputDir, entry, manifest, err, wasSkipped)) {
      ++ok;
    } else if (wasSkipped) {
      ++skipped;
    } else {
      std::cerr << "[" << entry.keyHex.substr(0, 16) << "...] " << err << "\n";
      ++failed;
    }
  }

  manifest.endManifest();
  std::cerr << "Converted " << ok << " program(s), " << skipped << " skipped, " << failed
            << " failed.\n";
  return failed == 0 ? 0 : 4;
}
