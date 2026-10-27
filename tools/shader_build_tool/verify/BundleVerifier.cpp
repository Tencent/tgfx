/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "verify/BundleVerifier.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "core/DiagnosticCodes.h"
#include "core/ShaderSources.h"
#include "gpu/BundleFormat.h"
#include "gpu/PrecompiledBundleIdentity.h"
#include "gpu/ShaderKeyHash.h"
#include "gpu/ShaderProfile.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "output/BundleManifest.h"
#include "zlib.h"
#include "zstd.h"

namespace tgfx {
namespace {

struct PoolEntryOnDisk {
  ShaderKeyHash hash;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  uint32_t reflOffset = 0;
};

struct BundleHeader {
  uint16_t formatVersion = 0;
  uint16_t compressionType = 0;
  uint64_t identityHash = 0;
  uint32_t toolchainABI = 0;
  uint32_t vertPoolCount = 0;
  uint32_t fragPoolCount = 0;
  uint32_t vertPoolOffset = 0;
  uint32_t fragPoolOffset = 0;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  uint32_t reflectionOffset = 0;
  std::string profileTag;
};

uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset) {
  return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
  uint32_t value = 0;
  for (int i = 0; i < 4; i++) {
    value |= static_cast<uint32_t>(data[offset + static_cast<size_t>(i)]) << (8 * i);
  }
  return value;
}

uint64_t ReadU64(const std::vector<uint8_t>& data, size_t offset) {
  uint64_t value = 0;
  for (int i = 0; i < 8; i++) {
    value |= static_cast<uint64_t>(data[offset + static_cast<size_t>(i)]) << (8 * i);
  }
  return value;
}

struct KeyPairLess {
  bool operator()(const std::pair<uint64_t, uint64_t>& left,
                  const std::pair<uint64_t, uint64_t>& right) const {
    if (left.first != right.first) {
      return left.first < right.first;
    }
    return left.second < right.second;
  }
};

std::string FormatKeyHex(const std::pair<uint64_t, uint64_t>& key) {
  char buffer[33];
  std::snprintf(buffer, sizeof(buffer), "%016llx%016llx",
                static_cast<unsigned long long>(key.first),
                static_cast<unsigned long long>(key.second));
  return buffer;
}

std::string NumberInHex(uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%llx", static_cast<unsigned long long>(value));
  return buffer;
}

// Reports one violation of the bundle verification. Every check below records through this
// helper, so the code, the backend (profile) and the severity stay in one shape for the cli
// renderer and the tests — the verifier never prints.
void Report(Diagnostics* diagnostics, Severity severity, const char* code, const char* profile,
            std::string message) {
  Diagnostic diagnostic;
  diagnostic.severity = severity;
  diagnostic.layer = Layer::Verify;
  diagnostic.code = code;
  diagnostic.profile = profile;
  diagnostic.message = std::move(message);
  diagnostics->report(std::move(diagnostic));
}

// Decompresses the stored data pool (uncompressed = dataSize bytes). Returns false when the
// stored compression type is unknown or the decompressed size disagrees with the header.
bool DecompressDataPool(const std::vector<uint8_t>& fileBytes, const BundleHeader& header,
                        std::vector<uint8_t>* uncompressed) {
  auto storedBytes = header.reflectionOffset - header.dataOffset;
  const uint8_t* storedData = fileBytes.data() + header.dataOffset;
  if (header.compressionType == BundleFormat::Compression::None) {
    if (storedBytes != header.dataSize) {
      return false;
    }
    uncompressed->assign(storedData, storedData + header.dataSize);
    return true;
  }
  if (header.compressionType == BundleFormat::Compression::Zlib) {
    uncompressed->resize(header.dataSize);
    auto destLen = static_cast<uLongf>(header.dataSize);
    auto status =
        uncompress(reinterpret_cast<Bytef*>(uncompressed->data()), &destLen,
                   reinterpret_cast<const Bytef*>(storedData), static_cast<uLong>(storedBytes));
    if (status != Z_OK || destLen != header.dataSize) {
      return false;
    }
    return true;
  }
  if (header.compressionType == BundleFormat::Compression::Zstd) {
    uncompressed->resize(header.dataSize);
    auto decoded = ZSTD_decompress(uncompressed->data(), header.dataSize, storedData, storedBytes);
    if (ZSTD_isError(decoded) || decoded != header.dataSize) {
      return false;
    }
    return true;
  }
  return false;
}

// The expected stage keys for one backend: the stage key hash of every (family, stage,
// permutation index) the rules can reach and the backend compiles. The mapped label set is a
// single entry in the normal case; more than one distinct label means two candidates collided
// on the same key, which must fail the verification instead of picking one silently.
std::map<std::pair<uint64_t, uint64_t>, std::set<std::string>, KeyPairLess> BuildExpectedKeys(
    ShaderProfile profile) {
  const std::string profileTag = ProfileTag(profile);
  std::map<std::pair<uint64_t, uint64_t>, std::set<std::string>, KeyPairLess> expected;
  for (const auto& factory : ShaderRegistry::All()) {
    auto shader = factory();
    auto info = shader->info();
    auto reachable = EnumerateReachablePermutations(info.name);
    if (!reachable) {
      continue;
    }
    for (const auto& pair : *reachable) {
      if (!PermutationCompilesForBackend(info, pair.first, pair.second, profile)) {
        continue;
      }
      auto vertHash = ComputeVertexKeyHash(info.name, pair.first, profileTag);
      expected[{vertHash.hi, vertHash.lo}].insert(info.name + VertexStageSuffix + "[" +
                                                  std::to_string(pair.first) + "]");
      auto fragHash = ComputeFragmentKeyHash(info.name, pair.second, profileTag);
      expected[{fragHash.hi, fragHash.lo}].insert(info.name + FragmentStageSuffix + "[" +
                                                  std::to_string(pair.second) + "]");
    }
  }
  return expected;
}

struct BundleCheckResult {
  size_t violations = 0;
  bool opened = false;
};

// Checks the manifest next to a bundle: that it exists, that it describes exactly this file, and
// (when a shader directory is given) that the sources it was built from are the current ones.
// Everything the bundle's own identity hash cannot say lives here, because that hash only covers
// the bundle's bytes: a bundle built from yesterday's shaders is perfectly self-consistent.
size_t CheckManifest(const std::string& bundlePath, ShaderProfile profile,
                     const std::vector<uint8_t>& fileBytes, const BundleHeader& header,
                     const std::string& shaderDir, const ShaderSourceSet* sources,
                     Diagnostics* diagnostics) {
  const std::string expectedTag = ProfileTag(profile);
  size_t violations = 0;
  auto manifestPath = ManifestPathFor(bundlePath);
  BundleManifest manifest;
  std::string error;
  if (!ReadBundleManifest(manifestPath, &manifest, &error)) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyManifest, expectedTag.c_str(),
           error + "; the bundle cannot be proven to match any sources");
    return 1;
  }
  if (manifest.manifestVersion != BundleManifestVersion) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyManifest, expectedTag.c_str(),
           "manifest version " + std::to_string(manifest.manifestVersion) + " (expected " +
               std::to_string(BundleManifestVersion) + ")");
    violations++;
  }
  auto slash = bundlePath.rfind('/');
  auto fileName = slash == std::string::npos ? bundlePath : bundlePath.substr(slash + 1);
  if (manifest.backend != expectedTag || manifest.file != fileName) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyManifest, expectedTag.c_str(),
           "manifest describes " + manifest.backend + "/" + manifest.file + ", not " +
               expectedTag + "/" + fileName);
    violations++;
  }
  if (manifest.fileSize != fileBytes.size() ||
      manifest.fileHash != HashBundleBytes(fileBytes.data(), fileBytes.size())) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyManifest, expectedTag.c_str(),
           "the bundle file is not the one its manifest was written for (size or content hash "
           "differs)");
    violations++;
  }
  if (manifest.identityHash != header.identityHash ||
      manifest.toolchainABI != header.toolchainABI ||
      manifest.formatVersion != header.formatVersion) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyManifest, expectedTag.c_str(),
           "manifest header fields disagree with the bundle header");
    violations++;
  }
  if (shaderDir.empty()) {
    Report(diagnostics, Severity::Warning, DiagnosticCode::VerifyFreshnessSkipped,
           expectedTag.c_str(), "not checked: pass --shader-dir to compare against the sources");
    return violations;
  }
  auto current = ComputeSourceDigest(*sources, profile);
  if (!current.ok) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyStale, expectedTag.c_str(),
           "cannot compute the current source digest: " + current.error);
    return violations + 1;
  }
  if (current.digest != manifest.sourceDigest) {
    auto message = "bundle is stale: built from sources with digest 0x" +
                   NumberInHex(manifest.sourceDigest) + ", the current sources in " + shaderDir +
                   " have 0x" + NumberInHex(current.digest) + " (" +
                   std::to_string(manifest.stageCount) + " stages recorded, " +
                   std::to_string(current.stageCount) + " now)";
    if (profile == ShaderProfile::Metal) {
      message += "; a Metal bundle's digest also covers the platform it was compiled for: verify "
                 "it with the same --metal-sdk and --metal-min-os";
    }
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyStale, expectedTag.c_str(),
           std::move(message));
    violations++;
  }
  return violations;
}

BundleCheckResult VerifyOneBundle(const std::string& path, ShaderProfile profile,
                                  const std::string& shaderDir, const ShaderSourceSet* sources,
                                  Diagnostics* diagnostics) {
  const std::string expectedTag = ProfileTag(profile);
  BundleCheckResult result;
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return result;
  }
  result.opened = true;
  std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
  if (fileBytes.size() < BundleFormat::HeaderSize) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleHeader, expectedTag.c_str(),
           "file smaller than header (" + std::to_string(fileBytes.size()) + " bytes)");
    result.violations++;
    return result;
  }

  BundleHeader header;
  auto magic = ReadU32(fileBytes, BundleFormat::Offset::Magic);
  header.formatVersion = ReadU16(fileBytes, BundleFormat::Offset::FormatVersion);
  header.compressionType = ReadU16(fileBytes, BundleFormat::Offset::CompressionType);
  header.identityHash = ReadU64(fileBytes, BundleFormat::Offset::IdentityHash);
  header.toolchainABI = ReadU32(fileBytes, BundleFormat::Offset::ToolchainABI);
  header.vertPoolCount = ReadU32(fileBytes, BundleFormat::Offset::VertPoolCount);
  header.fragPoolCount = ReadU32(fileBytes, BundleFormat::Offset::FragPoolCount);
  header.vertPoolOffset = ReadU32(fileBytes, BundleFormat::Offset::VertPoolOffset);
  header.fragPoolOffset = ReadU32(fileBytes, BundleFormat::Offset::FragPoolOffset);
  header.dataOffset = ReadU32(fileBytes, BundleFormat::Offset::DataOffset);
  header.dataSize = ReadU32(fileBytes, BundleFormat::Offset::DataSize);
  header.reflectionOffset = ReadU32(fileBytes, BundleFormat::Offset::ReflectionOffset);
  auto* tagBytes =
      reinterpret_cast<const char*>(fileBytes.data()) + BundleFormat::Offset::ProfileTag;
  header.profileTag = std::string(tagBytes, strnlen(tagBytes, BundleFormat::ProfileTagSize));

  if (magic != BundleFormat::Magic) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleHeader, expectedTag.c_str(),
           "bad magic 0x" + NumberInHex(magic));
    result.violations++;
  }
  if (header.formatVersion < BundleFormat::MinVerifiableVersion ||
      header.formatVersion > BundleFormat::CurrentVersion) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleHeader, expectedTag.c_str(),
           "format version " + std::to_string(header.formatVersion) + " (expected " +
               std::to_string(BundleFormat::MinVerifiableVersion) + " to " +
               std::to_string(BundleFormat::CurrentVersion) + ")");
    result.violations++;
  }
  if (header.toolchainABI != ExpectedToolchainABI) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleHeader, expectedTag.c_str(),
           "toolchain ABI 0x" + NumberInHex(header.toolchainABI) + " (expected 0x" +
               NumberInHex(ExpectedToolchainABI) + "); bundle predates the current contract");
    result.violations++;
  }
  if (header.profileTag != expectedTag) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleHeader, expectedTag.c_str(),
           "embedded profile tag \"" + header.profileTag + "\" does not match the backend");
    result.violations++;
  }
  // All layout arithmetic runs in size_t: the u32 fields can carry counts whose 28-byte strides
  // overflow 32 bits (0x40000000 entries wrap to zero), which would otherwise slip past these
  // checks and read past the end of the file below.
  auto vertPoolEnd = BundleFormat::HeaderSize + static_cast<size_t>(header.vertPoolCount) * BundleFormat::PoolEntrySize;
  auto fragPoolEnd = static_cast<size_t>(header.fragPoolOffset) +
                     static_cast<size_t>(header.fragPoolCount) * BundleFormat::PoolEntrySize;
  if (header.vertPoolOffset != BundleFormat::HeaderSize || header.fragPoolOffset != vertPoolEnd ||
      header.dataOffset != fragPoolEnd || header.reflectionOffset < header.dataOffset ||
      header.reflectionOffset > fileBytes.size()) {
    // Bail out before any pool arithmetic below: a bad layout would underflow the stored-bytes
    // subtraction or read out of bounds.
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleHeader, expectedTag.c_str(),
           "inconsistent pool layout (vertPoolOffset=" + std::to_string(header.vertPoolOffset) +
               " fragPoolOffset=" + std::to_string(header.fragPoolOffset) +
               " dataOffset=" + std::to_string(header.dataOffset) + " reflectionOffset=" +
               std::to_string(header.reflectionOffset) + " fileSize=" +
               std::to_string(fileBytes.size()) + ")");
    result.violations++;
    return result;
  }

  std::vector<uint8_t> dataPool;
  if (!DecompressDataPool(fileBytes, header, &dataPool)) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleHeader, expectedTag.c_str(),
           "data pool decompression failed or size mismatch");
    result.violations++;
    return result;
  }

  auto expected = BuildExpectedKeys(profile);
  auto reflPoolSize = fileBytes.size() - header.reflectionOffset;
  std::set<std::pair<uint64_t, uint64_t>, KeyPairLess> storedKeys;
  size_t poolViolations = 0;
  for (uint32_t pool = 0; pool < 2; pool++) {
    bool isVertexPool = pool == 0;
    auto count = isVertexPool ? header.vertPoolCount : header.fragPoolCount;
    auto poolOffset = isVertexPool ? header.vertPoolOffset : header.fragPoolOffset;
    for (uint32_t i = 0; i < count; i++) {
      auto entryOffset = poolOffset + static_cast<size_t>(i) * BundleFormat::PoolEntrySize;
      if (entryOffset + BundleFormat::PoolEntrySize > fileBytes.size()) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundlePool,
               expectedTag.c_str(),
               std::string(isVertexPool ? "vert" : "frag") + " pool entry " +
                   std::to_string(i) + " lies outside the file (offset " +
                   std::to_string(entryOffset) + ", file " + std::to_string(fileBytes.size()) +
                   " bytes)");
        poolViolations++;
        continue;
      }
      PoolEntryOnDisk entry;
      entry.hash.hi = ReadU64(fileBytes, entryOffset + BundleFormat::EntryOffset::HashHi);
      entry.hash.lo = ReadU64(fileBytes, entryOffset + BundleFormat::EntryOffset::HashLo);
      entry.dataOffset = ReadU32(fileBytes, entryOffset + BundleFormat::EntryOffset::DataOffset);
      entry.dataSize = ReadU32(fileBytes, entryOffset + BundleFormat::EntryOffset::DataSize);
      entry.reflOffset =
          ReadU32(fileBytes, entryOffset + BundleFormat::EntryOffset::ReflectionOffset);
      auto key = std::make_pair(entry.hash.hi, entry.hash.lo);
      if (!storedKeys.insert(key).second) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundlePool,
               expectedTag.c_str(),
               std::string(isVertexPool ? "vert" : "frag") + " entry " + std::to_string(i) +
                   " key " + FormatKeyHex(key) + " appears more than once in the pool");
        poolViolations++;
      }
      auto candidate = expected.find(key);
      if (candidate == expected.end()) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundlePool,
               expectedTag.c_str(),
               std::string(isVertexPool ? "vert" : "frag") + " entry " + std::to_string(i) +
                   " key " + FormatKeyHex(key) + " has no rule-reachable candidate");
        poolViolations++;
      } else if (candidate->second.size() > 1) {
        std::string labels;
        for (const auto& label : candidate->second) {
          labels += " " + label;
        }
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundlePool,
               expectedTag.c_str(),
               std::string(isVertexPool ? "vert" : "frag") + " entry " + std::to_string(i) +
                   " key " + FormatKeyHex(key) + " maps to multiple candidates:" + labels);
        poolViolations++;
      }
      if (static_cast<size_t>(entry.dataOffset) + entry.dataSize > dataPool.size()) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundlePool,
               expectedTag.c_str(),
               std::string(isVertexPool ? "vert" : "frag") + " entry " + std::to_string(i) +
                   " data range [" + std::to_string(entry.dataOffset) + ", " +
                   std::to_string(static_cast<size_t>(entry.dataOffset) + entry.dataSize) +
                   ") exceeds the data pool (" + std::to_string(dataPool.size()) + " bytes)");
        poolViolations++;
      }
      if (static_cast<size_t>(entry.reflOffset) + 4 > reflPoolSize) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundlePool,
               expectedTag.c_str(),
               std::string(isVertexPool ? "vert" : "frag") + " entry " + std::to_string(i) +
                   " reflection offset " + std::to_string(entry.reflOffset) +
                   " outside the reflection pool");
        poolViolations++;
      }
    }
  }
  result.violations += poolViolations;

  size_t missing = 0;
  for (const auto& entry : expected) {
    if (storedKeys.find(entry.first) == storedKeys.end()) {
      std::string labels;
      for (const auto& label : entry.second) {
        labels += " " + label;
      }
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleMissing,
             expectedTag.c_str(),
             "expected entry missing from bundle: " + FormatKeyHex(entry.first) + labels);
      missing++;
    }
  }
  result.violations += missing;

  // Identity hash recomputation over the same field sequence the writer fed: header fields,
  // pool entries, the uncompressed data pool, and the reflection pool. A mismatch means the
  // file was modified or truncated after it was written.
  char tagBuf[BundleFormat::ProfileTagSize] = {};
  std::strncpy(tagBuf, header.profileTag.c_str(), sizeof(tagBuf) - 1);
  uint64_t identity = BundleIdentityHashInit();
  identity =
      BundleIdentityHashHeader(identity, header.formatVersion, header.vertPoolCount,
                               header.fragPoolCount, reinterpret_cast<const uint8_t*>(tagBuf));
  for (uint32_t pool = 0; pool < 2; pool++) {
    bool isVertexPool = pool == 0;
    auto count = isVertexPool ? header.vertPoolCount : header.fragPoolCount;
    auto poolOffset = isVertexPool ? header.vertPoolOffset : header.fragPoolOffset;
    for (uint32_t i = 0; i < count; i++) {
      auto entryOffset = poolOffset + static_cast<size_t>(i) * BundleFormat::PoolEntrySize;
      identity = BundleIdentityHashEntry(
          identity, ReadU64(fileBytes, entryOffset + BundleFormat::EntryOffset::HashHi),
          ReadU64(fileBytes, entryOffset + BundleFormat::EntryOffset::HashLo),
          ReadU32(fileBytes, entryOffset + BundleFormat::EntryOffset::DataOffset),
          ReadU32(fileBytes, entryOffset + BundleFormat::EntryOffset::DataSize),
          ReadU32(fileBytes, entryOffset + BundleFormat::EntryOffset::ReflectionOffset));
    }
  }
  identity = BundleIdentityHashBytes(identity, dataPool.data(), dataPool.size());
  identity = BundleIdentityHashBytes(identity, fileBytes.data() + header.reflectionOffset,
                                     fileBytes.size() - header.reflectionOffset);
  if (identity != header.identityHash) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBundleIdentity,
           expectedTag.c_str(),
           "identity hash mismatch (stored 0x" + NumberInHex(header.identityHash) +
               ", recomputed 0x" + NumberInHex(identity) + ")");
    result.violations++;
  }

  result.violations += CheckManifest(path, profile, fileBytes, header, shaderDir, sources,
                                     diagnostics);

  return result;
}

}  // namespace

int VerifyBundles(const std::string& bundleDir, const std::string& shaderDir,
                  const std::optional<std::vector<std::string>>& requiredBackends,
                  Diagnostics* diagnostics, BundleVerifyCounts* counts) {
  std::set<ShaderProfile> required;
  if (requiredBackends) {
    // A typo must not turn into "nothing is required": reject names that are not profiles, and
    // an empty list, before any bundle is looked at.
    if (requiredBackends->empty()) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBackendList, "",
             "--require-backends needs at least one backend");
      return 1;
    }
    for (const auto& name : *requiredBackends) {
      auto profile = ParseProfileTag(name);
      if (!profile) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBackendList, "",
               "unknown backend \"" + name + "\" in --require-backends");
        return 1;
      }
      required.insert(*profile);
    }
  }
  // The current sources are read once for every bundle checked, the same way a build reads them.
  ShaderSourceSet sources;
  if (!shaderDir.empty()) {
    sources = LoadShaderSources(shaderDir);
    for (const auto& error : sources.errors) {
      Report(diagnostics, Severity::Error, DiagnosticCode::SourcesUnreadable, "", error);
    }
  }
  int totalViolations = 0;
  size_t openedBundles = 0;
  size_t absentNotRequired = 0;
  for (auto profile : AllShaderProfiles) {
    const std::string tag = ProfileTag(profile);
    auto path = bundleDir + "/shader_bundle." + tag + ".bin";
    auto result = VerifyOneBundle(path, profile, shaderDir, &sources, diagnostics);
    if (result.opened) {
      openedBundles++;
    } else if (required.count(profile) > 0) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyRequiredMissing, tag.c_str(),
             "required bundle is missing: " + path);
      result.violations++;
    } else {
      absentNotRequired++;
    }
    totalViolations += static_cast<int>(result.violations);
  }
  if (counts != nullptr) {
    counts->openedBundles = openedBundles;
    counts->absentNotRequired = absentNotRequired;
    counts->requiredGiven = requiredBackends.has_value();
  }
  if (openedBundles == 0) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyNoBundles, "",
           "no bundles found in " + bundleDir);
    totalViolations++;
  }
  return totalViolations == 0 ? 0 : 1;
}

}  // namespace tgfx
