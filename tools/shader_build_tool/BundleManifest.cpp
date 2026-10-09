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

#include "BundleManifest.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "ShaderCompiler.h"
#include "ShaderSources.h"
#include "gpu/PrecompiledBundleIdentity.h"
#include "gpu/shaders/PermutationRules.h"
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {
namespace {

constexpr uint64_t kDigestSeed = 0x54475346534F5552ULL;  // "TGSFSOUR"

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

  uint64_t state = kDigestSeed;
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

uint32_t ReadLE32(const std::vector<uint8_t>& bytes, size_t offset) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) {
    value |= static_cast<uint32_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}

uint64_t ReadLE64(const std::vector<uint8_t>& bytes, size_t offset) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
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

SourceDigestResult ComputeSourceDigest(const ShaderSourceSet& sources, const std::string& backend) {
  SourceDigestResult result;
  if (!sources.ok) {
    result.error = sources.errors.empty() ? "the shader sources failed to load" : sources.errors[0];
    return result;
  }
  DigestStream stream;
  stream.feedString("tgfx-shader-source-digest-v1");
  stream.feedU32(kExpectedToolchainABI);
  stream.feedString(backend);
  if (backend == "metal") {
    stream.feedString(MetalCompilerFingerprint());
  }

  // The set is ordered by shader name, so the digest does not depend on registration order, which
  // follows static initialization across translation units.
  for (const auto& shader : sources.shaders) {
    const auto& info = shader.info;
    auto reachable = EnumerateReachablePermutations(info.name);
    if (!reachable) {
      result.error = "no rule enumerator for " + info.name;
      return result;
    }
    stream.feedString(info.name);
    stream.feedString(info.vertexFile);
    stream.feedString(info.fragmentFile);
    stream.feedString(shader.vertex);
    stream.feedString(shader.fragment);
    auto vertDomain = info.vertDomain;
    auto fragDomain = info.fragDomain;
    for (const auto& permutation : *reachable) {
      if (!PermutationCompilesForBackend(info, permutation.first, permutation.second, backend)) {
        continue;
      }
      stream.feedU32(permutation.first);
      stream.feedU32(permutation.second);
      stream.feedStrings(vertDomain.defineListFor(permutation.first));
      stream.feedStrings(fragDomain.defineListFor(permutation.second));
      result.stageCount++;
    }
    result.shaderCount++;
  }
  result.digest = stream.state;
  result.ok = true;
  return result;
}

uint64_t HashBundleBytes(const uint8_t* data, size_t size) {
  return BundleIdentityHashBytes(0xCBF29CE484222325ULL, data, size);
}

std::string ManifestPathFor(const std::string& bundlePath) {
  const std::string suffix = ".bin";
  if (bundlePath.size() > suffix.size() &&
      bundlePath.compare(bundlePath.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return bundlePath.substr(0, bundlePath.size() - suffix.size()) + ".manifest";
  }
  return bundlePath + ".manifest";
}

bool WriteBundleManifest(const std::string& bundlePath, const std::string& backend,
                         const SourceDigestResult& digest, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!ReadBinaryFile(bundlePath, &bytes) || bytes.size() < 80) {
    *error = "cannot read back the bundle " + bundlePath;
    return false;
  }
  auto slash = bundlePath.rfind('/');
  auto fileName = slash == std::string::npos ? bundlePath : bundlePath.substr(slash + 1);
  auto manifestPath = ManifestPathFor(bundlePath);
  std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    *error = "cannot write " + manifestPath;
    return false;
  }
  out << "# tgfx shader bundle manifest, generated by shader_build_tool. Do not edit.\n";
  out << "manifestVersion=" << kBundleManifestVersion << "\n";
  out << "backend=" << backend << "\n";
  out << "file=" << fileName << "\n";
  out << "fileSize=" << bytes.size() << "\n";
  out << "fileHash=" << Hex64(HashBundleBytes(bytes.data(), bytes.size())) << "\n";
  out << "identityHash=" << Hex64(ReadLE64(bytes, 8)) << "\n";
  out << "toolchainABI=" << Hex64(ReadLE32(bytes, 16)) << "\n";
  out << "formatVersion=" << (bytes[4] | (bytes[5] << 8)) << "\n";
  out << "sourceDigest=" << Hex64(digest.digest) << "\n";
  out << "shaderCount=" << digest.shaderCount << "\n";
  out << "stageCount=" << digest.stageCount << "\n";
  out.close();
  if (!out) {
    *error = "failed while writing " + manifestPath;
    return false;
  }
  return true;
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
