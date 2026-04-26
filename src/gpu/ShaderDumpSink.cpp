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

#include "ShaderDumpSink.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include "gpu/ProgramInfo.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/GeometryProcessor.h"
#include "gpu/processors/XferProcessor.h"

namespace tgfx {
namespace {

// Caches the env-var dir on first lookup. Empty string means disabled.
const std::string& DumpDir() {
  static const std::string dir = []() -> std::string {
    const char* env = std::getenv("TGFX_SHADER_DUMP_DIR");
    return (env && *env) ? std::string(env) : std::string();
  }();
  return dir;
}

// Create <dir> and <dir>/shaders if missing. Idempotent.
void EnsureDirs(const std::string& dir) {
  auto mk = [](const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) {
      mkdir(p.c_str(), 0755);
    }
  };
  mk(dir);
  mk(dir + "/shaders");
}

std::string KeyToHex(const BytesKey& key) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(key.size() * 8);
  const uint32_t* data = key.data();
  for (size_t i = 0; i < key.size(); ++i) {
    uint32_t v = data[i];
    for (int b = 7; b >= 0; --b) {
      out.push_back(kHex[(v >> (b * 4)) & 0xF]);
    }
  }
  return out;
}

// Escape a string for inclusion in a JSON string literal. We only handle the subset the dumper
// actually emits (processor names — ASCII with no control chars — and file paths).
std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
    }
  }
  return out;
}

void WriteFileOnce(const std::string& path, const std::string& text, bool* writtenOut) {
  struct stat st;
  if (stat(path.c_str(), &st) == 0) {
    if (writtenOut) *writtenOut = true;
    return;
  }
  auto* f = std::fopen(path.c_str(), "w");
  if (!f) {
    if (writtenOut) *writtenOut = false;
    return;
  }
  auto written = std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
  if (writtenOut) *writtenOut = (written == text.size());
}

std::string BuildTupleJson(const ProgramInfo* info) {
  std::string j;
  j += "\"tuple\":{";
  j += "\"geometryProcessor\":\"";
  j += JsonEscape(info->getGeometryProcessor()->name());
  j += "\",\"fragmentProcessors\":[";
  for (size_t i = 0; i < info->numFragmentProcessors(); ++i) {
    if (i > 0) j += ",";
    j += "\"";
    j += JsonEscape(info->getFragmentProcessor(i)->name());
    j += "\"";
  }
  j += "],\"numColorProcessors\":";
  j += std::to_string(info->numColorFragmentProcessors());
  j += ",\"xferProcessor\":\"";
  j += JsonEscape(info->getXferProcessor()->name());
  j += "\"}";
  return j;
}

void AppendJsonlLine(const std::string& dir, const std::string& keyHex, const ProgramInfo* info) {
  std::string line = "{\"keyHex\":\"";
  line += keyHex;
  line += "\",\"vsFile\":\"shaders/";
  line += keyHex;
  line += ".vert.glsl\",\"fsFile\":\"shaders/";
  line += keyHex;
  line += ".frag.glsl\",";
  line += BuildTupleJson(info);
  line += "}\n";
  auto* f = std::fopen((dir + "/programs.jsonl").c_str(), "a");
  if (!f) return;
  std::fwrite(line.data(), 1, line.size(), f);
  std::fclose(f);
}

std::mutex& SinkMutex() {
  static std::mutex m;
  return m;
}

std::unordered_set<std::string>& SeenKeys() {
  static std::unordered_set<std::string> s;
  return s;
}

}  // namespace

bool ShaderDumpSink::Enabled() {
  return !DumpDir().empty();
}

void ShaderDumpSink::Record(const BytesKey& programKey, const ProgramInfo* programInfo,
                            const std::string& vertexText, const std::string& fragmentText) {
  if (!Enabled() || programInfo == nullptr) {
    return;
  }
  auto keyHex = KeyToHex(programKey);
  std::lock_guard<std::mutex> lock(SinkMutex());
  auto& dir = DumpDir();
  auto& seen = SeenKeys();
  if (!seen.insert(keyHex).second) {
    // Duplicate key within this process — shader text is guaranteed identical since the key
    // is the cache key. Nothing further to do.
    return;
  }
  EnsureDirs(dir);
  bool vsOk = false;
  bool fsOk = false;
  WriteFileOnce(dir + "/shaders/" + keyHex + ".vert.glsl", vertexText, &vsOk);
  WriteFileOnce(dir + "/shaders/" + keyHex + ".frag.glsl", fragmentText, &fsOk);
  if (!vsOk || !fsOk) {
    // File write failed — omit the JSONL line to keep index/file consistency. Un-insert from
    // SeenKeys so a future attempt can retry.
    seen.erase(keyHex);
    return;
  }
  AppendJsonlLine(dir, keyHex, programInfo);
}

}  // namespace tgfx
