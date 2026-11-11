//////////////////////////////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////////////////////////////

#include "output/BundleManifest.h"
#include <algorithm>
#include <filesystem>
#include "gpu/shaders/BundleCodec.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "output/OutputFile.h"
#include "compile/ShaderCompiler.h"
#include <spirv_cross_c.h>
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "core/CapacityDefines.h"
#include "gpu/shaders/KernelContract.h"

namespace tgfx {
namespace {

// The manifest's two hash seeds, named once: the source digest's domain separation and the file
// hash's FNV offset basis. Same algorithm as the bundle identity, different purposes.
constexpr uint64_t SourceDigestSeed = 0x54475346534F5552ULL;  // "TGSFSOUR"
constexpr uint64_t FileHashSeed = 0xCBF29CE484222325ULL;

// A canonical byte stream: every string is length prefixed and every integer is fed as fixed
// width little-endian, so two different field sequences can never produce the same bytes.
class DigestStream {
 public:
  void feedU32(uint32_t value) {
    uint8_t le[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                     static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    state = BundleIdentityHashBytes(state, le, 4);
  }

  void feedU64(uint64_t value) {
    uint8_t le[8] = {};
    for (int i = 0; i < 8; ++i) {
      le[i] = static_cast<uint8_t>(value >> (i * 8));
    }
    state = BundleIdentityHashBytes(state, le, 8);
  }

  void feedString(const std::string& text) {
    feedU64(text.size());
    state = BundleIdentityHashBytes(state, text.data(), text.size());
  }

  void feedStrings(const std::vector<std::string>& list) {
    feedU64(list.size());
    for (const auto& text : list) {
      feedString(text);
    }
  }

  uint64_t state = SourceDigestSeed;
};

std::string Hex64(uint64_t value) {
  char buffer[19];
  std::snprintf(buffer, sizeof(buffer), "0x%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

bool ReadBinaryFile(const std::string& path, std::vector<uint8_t>* bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

bool ParseHex(const std::string& text, uint64_t* value) {
  if (text.size() < 3 || text[0] != '0' || text[1] != 'x') {
    return false;
  }
  char* end = nullptr;
  *value = std::strtoull(text.c_str() + 2, &end, 16);
  return end != nullptr && *end == '\0';
}

bool ParseDecimal(const std::string& text, uint64_t* value) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  *value = std::strtoull(text.c_str(), &end, 10);
  return end != nullptr && *end == '\0';
}

}  // namespace

SourceDigestResult ComputeSourceDigest(const std::string& shaderDir, ShaderProfile profile,
                                       const std::string& generatorContract) {
  SourceDigestResult result;
  std::error_code walkError;
  if (!std::filesystem::is_directory(shaderDir, walkError) || walkError) {
    result.error = "the shader directory " + shaderDir + " cannot be read";
    return result;
  }
  DigestStream stream;
  stream.feedString("tgfx-shader-source-digest-v2");
  stream.feedU32(ExpectedToolchainABI);
  stream.feedString(ProfileTag(profile));
  // The binding contract macros ride with the digest: a layout value change must invalidate
  // every bundle even though the shader text did not move, because the compiled resource layout
  // moved with it.
  for (const auto& contractDefine : BindingContractDefines()) {
    stream.feedString(contractDefine);
  }
  for (const auto& opDefine : Op::Defines()) {
    stream.feedString(opDefine);
  }
  for (const auto& capacityDefine : SlotCapacityDefines()) {
    stream.feedString(capacityDefine);
  }
  // The operator table and the data-pool field lists generate the stage text through the
  // chain-ops pragma, so a change there changes what was compiled without touching any shader
  // source. They are C++ headers, so the directory walk below cannot see them.
  stream.feedString(generatorContract);
  // The freshness contract stops at the sources: the tool's own build, the vendored translators
  // and the compiler versions change what the same text compiles to, and are covered by the
  // toolchain ABI discipline and by regenerating and comparing the bundle — not by this digest.
  if (profile == ShaderProfile::Metal) {
    stream.feedString(MetalCompilerFingerprint());
  } else if (profile == ShaderProfile::D3D12) {
    // D3D12 supports both a source form and a compiled form, so the digest must record which one
    // this build produces: switching between them otherwise leaves a bundle whose digest matches
    // while its bytes mean something else.
    stream.feedString(HLSLCompilerFingerprint());
  }
  // The whole shader tree, in a canonical order: every whitelisted file's path and bytes. A
  // change anywhere — a module hook, a shared include — moves the digest; the paths make the
  // walk independent of the directory iteration order.
  std::vector<std::string> paths = {};
  for (std::filesystem::recursive_directory_iterator it(shaderDir, walkError), end;
       !walkError && it != end; it.increment(walkError)) {
    if (walkError || !it->is_regular_file(walkError)) {
      continue;
    }
    const auto extension = it->path().extension().string();
    if (extension != ".glsl" && extension != ".inc" && extension != ".vert" &&
        extension != ".frag") {
      continue;
    }
    auto relative = std::filesystem::relative(it->path(), shaderDir, walkError).generic_string();
    if (walkError) {
      result.error = "cannot resolve " + it->path().string() + " against " + shaderDir;
      return result;
    }
    paths.push_back(relative);
  }
  if (walkError) {
    result.error = "cannot walk " + shaderDir + ": " + walkError.message();
    return result;
  }
  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) {
    std::ifstream file(shaderDir + "/" + path, std::ios::binary);
    if (!file.is_open()) {
      result.error = "cannot read " + path + " under " + shaderDir;
      return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    stream.feedString(path);
    stream.feedString(buffer.str());
    result.shaderCount++;
  }
  // Both counters describe the same tree now: the per-stage counting retired with the loaded
  // source set, and the stale report prints these as files.
  result.stageCount = result.shaderCount;
  result.digest = stream.state;
  result.ok = true;
  return result;
}

uint64_t HashBundleBytes(const uint8_t* data, size_t size) {
  return BundleIdentityHashBytes(FileHashSeed, data, size);
}

std::string ManifestPathFor(const std::string& bundlePath) {
  const std::string suffix = ".bin";
  if (bundlePath.size() > suffix.size() &&
      bundlePath.compare(bundlePath.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return bundlePath.substr(0, bundlePath.size() - suffix.size()) + ".manifest";
  }
  return bundlePath + ".manifest";
}

bool WriteBundleManifest(const std::string& bundlePath, ShaderProfile profile,
                         const SourceDigestResult& digest, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!ReadBinaryFile(bundlePath, &bytes) || bytes.size() < BundleFormat::HeaderSize) {
    *error = "cannot read back the bundle " + bundlePath;
    return false;
  }
  auto slash = bundlePath.rfind('/');
  auto fileName = slash == std::string::npos ? bundlePath : bundlePath.substr(slash + 1);
  auto manifestPath = ManifestPathFor(bundlePath);
  std::ostringstream out;
  out << "# tgfx shader bundle manifest, generated by shader_build_tool. Do not edit.\n";
  out << "manifestVersion=" << BundleManifestVersion << "\n";
  out << "backend=" << ProfileTag(profile) << "\n";
  out << "file=" << fileName << "\n";
  out << "fileSize=" << bytes.size() << "\n";
  out << "fileHash=" << Hex64(HashBundleBytes(bytes.data(), bytes.size())) << "\n";
  BundleHeader header = {};
  ParseBundleHeader(bytes.data(), bytes.size(), &header);  // the size is checked above
  out << "identityHash=" << Hex64(header.identityHash) << "\n";
  out << "toolchainABI=" << Hex64(header.toolchainABI) << "\n";
  out << "formatVersion=" << header.formatVersion << "\n";
  out << "sourceDigest=" << Hex64(digest.digest) << "\n";
  out << "shaderCount=" << digest.shaderCount << "\n";
  out << "stageCount=" << digest.stageCount << "\n";
  return WriteFileIfChanged(manifestPath, out.str(), nullptr, error);
}

bool ReadBundleManifest(const std::string& manifestPath, BundleManifest* manifest,
                        std::string* error) {
  std::ifstream in(manifestPath, std::ios::binary);
  if (!in.is_open()) {
    *error = "no manifest at " + manifestPath;
    return false;
  }
  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto equals = line.find('=');
    if (equals == std::string::npos) {
      *error = "malformed manifest line \"" + line + "\"";
      return false;
    }
    values[line.substr(0, equals)] = line.substr(equals + 1);
  }
  auto field = [&](const char* key, std::string* out) {
    auto it = values.find(key);
    if (it == values.end()) {
      *error = std::string("manifest is missing \"") + key + "\"";
      return false;
    }
    *out = it->second;
    return true;
  };
  auto number = [&](const char* key, uint64_t* out) {
    std::string text;
    if (!field(key, &text)) {
      return false;
    }
    if (!(text.rfind("0x", 0) == 0 ? ParseHex(text, out) : ParseDecimal(text, out))) {
      *error = std::string("manifest value of \"") + key + "\" is not a number: " + text;
      return false;
    }
    return true;
  };
  uint64_t manifestVersion = 0;
  uint64_t fileSize = 0;
  uint64_t fileHash = 0;
  uint64_t identityHash = 0;
  uint64_t abi = 0;
  uint64_t formatVersion = 0;
  uint64_t sourceDigest = 0;
  uint64_t shaderCount = 0;
  uint64_t stageCount = 0;
  if (!number("manifestVersion", &manifestVersion) || !field("backend", &manifest->backend) ||
      !field("file", &manifest->file) || !number("fileSize", &fileSize) ||
      !number("fileHash", &fileHash) || !number("identityHash", &identityHash) ||
      !number("toolchainABI", &abi) || !number("formatVersion", &formatVersion) ||
      !number("sourceDigest", &sourceDigest) || !number("shaderCount", &shaderCount) ||
      !number("stageCount", &stageCount)) {
    return false;
  }
  manifest->manifestVersion = static_cast<uint32_t>(manifestVersion);
  manifest->fileSize = fileSize;
  manifest->fileHash = fileHash;
  manifest->identityHash = identityHash;
  manifest->toolchainABI = static_cast<uint32_t>(abi);
  manifest->formatVersion = static_cast<uint16_t>(formatVersion);
  manifest->sourceDigest = sourceDigest;
  manifest->shaderCount = shaderCount;
  manifest->stageCount = stageCount;
  return true;
}

}  // namespace tgfx
