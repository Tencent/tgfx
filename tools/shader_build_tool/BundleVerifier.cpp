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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "BundleVerifier.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "BundleWriter.h"
#include "gpu/PrecompiledBundleIdentity.h"
#include "gpu/shaders/PermutationRules.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "zlib.h"
#include "zstd.h"

namespace tgfx {
namespace {

constexpr uint32_t BUNDLE_MAGIC = 0x54475346u;  // "TGSF"
constexpr uint32_t HEADER_SIZE = 80;
constexpr uint32_t POOL_ENTRY_SIZE = 28;
constexpr uint16_t FORMAT_VERSION_V4 = 4;

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

// Decompresses the stored data pool (uncompressed = dataSize bytes). Returns false when the
// stored compression type is unknown or the decompressed size disagrees with the header.
bool DecompressDataPool(const std::vector<uint8_t>& fileBytes, const BundleHeader& header,
                        std::vector<uint8_t>* uncompressed) {
  auto storedBytes = header.reflectionOffset - header.dataOffset;
  const uint8_t* storedData = fileBytes.data() + header.dataOffset;
  if (header.compressionType == 0) {
    if (storedBytes != header.dataSize) {
      return false;
    }
    uncompressed->assign(storedData, storedData + header.dataSize);
    return true;
  }
  if (header.compressionType == 1) {
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
  if (header.compressionType == 2) {
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
    const std::string& profileTag) {
  std::map<std::pair<uint64_t, uint64_t>, std::set<std::string>, KeyPairLess> expected;
  for (const auto& factory : ShaderRegistry::All()) {
    auto shader = factory();
    auto info = shader->info();
    auto reachable = EnumerateReachablePermutations(info.name);
    if (!reachable) {
      continue;
    }
    for (const auto& pair : *reachable) {
      if (!PermutationCompilesForBackend(info, pair.first, pair.second, profileTag)) {
        continue;
      }
      auto vertHash = ComputeShaderKeyHash(info.name + "_Vert", pair.first, profileTag);
      expected[{vertHash.hi, vertHash.lo}].insert(info.name + "_Vert[" +
                                                  std::to_string(pair.first) + "]");
      auto fragHash = ComputeShaderKeyHash(info.name + "_Frag", pair.second, profileTag);
      expected[{fragHash.hi, fragHash.lo}].insert(info.name + "_Frag[" +
                                                  std::to_string(pair.second) + "]");
    }
  }
  return expected;
}

struct BundleCheckResult {
  size_t violations = 0;
  bool opened = false;
};

BundleCheckResult VerifyOneBundle(const std::string& path, const std::string& expectedTag) {
  BundleCheckResult result;
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    std::cout << "[verify] " << expectedTag << ": no bundle at " << path << " (skipped)\n";
    return result;
  }
  result.opened = true;
  std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
  if (fileBytes.size() < HEADER_SIZE) {
    std::cout << "[verify] " << expectedTag << ": VIOLATION file smaller than header ("
              << fileBytes.size() << " bytes)\n";
    result.violations++;
    return result;
  }

  BundleHeader header;
  auto magic = ReadU32(fileBytes, 0);
  header.formatVersion = ReadU16(fileBytes, 4);
  header.compressionType = ReadU16(fileBytes, 6);
  header.identityHash = ReadU64(fileBytes, 8);
  header.toolchainABI = ReadU32(fileBytes, 16);
  header.vertPoolCount = ReadU32(fileBytes, 20);
  header.fragPoolCount = ReadU32(fileBytes, 24);
  header.vertPoolOffset = ReadU32(fileBytes, 28);
  header.fragPoolOffset = ReadU32(fileBytes, 32);
  header.dataOffset = ReadU32(fileBytes, 36);
  header.dataSize = ReadU32(fileBytes, 40);
  header.reflectionOffset = ReadU32(fileBytes, 44);
  header.profileTag =
      std::string(reinterpret_cast<const char*>(fileBytes.data()) + 48,
                  strnlen(reinterpret_cast<const char*>(fileBytes.data()) + 48, 32));

  if (magic != BUNDLE_MAGIC) {
    std::cout << "[verify] " << expectedTag << ": VIOLATION bad magic 0x" << std::hex << magic
              << std::dec << "\n";
    result.violations++;
  }
  if (header.formatVersion != FORMAT_VERSION_V4) {
    std::cout << "[verify] " << expectedTag << ": VIOLATION format version " << header.formatVersion
              << " (expected " << FORMAT_VERSION_V4 << ")\n";
    result.violations++;
  }
  if (header.toolchainABI != kExpectedToolchainABI) {
    std::cout << "[verify] " << expectedTag << ": VIOLATION toolchain ABI 0x" << std::hex
              << header.toolchainABI << " (expected 0x" << kExpectedToolchainABI << std::dec
              << ") — bundle predates the current contract\n";
    result.violations++;
  }
  if (header.profileTag != expectedTag) {
    std::cout << "[verify] " << expectedTag << ": VIOLATION embedded profile tag \""
              << header.profileTag << "\" does not match the backend\n";
    result.violations++;
  }
  // All layout arithmetic runs in size_t: the u32 fields can carry counts whose 28-byte strides
  // overflow 32 bits (0x40000000 entries wrap to zero), which would otherwise slip past these
  // checks and read past the end of the file below.
  auto vertPoolEnd = HEADER_SIZE + static_cast<size_t>(header.vertPoolCount) * POOL_ENTRY_SIZE;
  auto fragPoolEnd = static_cast<size_t>(header.fragPoolOffset) +
                     static_cast<size_t>(header.fragPoolCount) * POOL_ENTRY_SIZE;
  if (header.vertPoolOffset != HEADER_SIZE || header.fragPoolOffset != vertPoolEnd ||
      header.dataOffset != fragPoolEnd || header.reflectionOffset < header.dataOffset ||
      header.reflectionOffset > fileBytes.size()) {
    // Bail out before any pool arithmetic below: a bad layout would underflow the stored-bytes
    // subtraction or read out of bounds.
    std::cout << "[verify] " << expectedTag << ": VIOLATION inconsistent pool layout ("
              << "vertPoolOffset=" << header.vertPoolOffset
              << " fragPoolOffset=" << header.fragPoolOffset << " dataOffset=" << header.dataOffset
              << " reflectionOffset=" << header.reflectionOffset << " fileSize=" << fileBytes.size()
              << ")\n";
    result.violations++;
    return result;
  }

  std::vector<uint8_t> dataPool;
  if (!DecompressDataPool(fileBytes, header, &dataPool)) {
    std::cout << "[verify] " << expectedTag
              << ": VIOLATION data pool decompression failed or size mismatch\n";
    result.violations++;
    return result;
  }

  auto expected = BuildExpectedKeys(expectedTag);
  auto reflPoolSize = fileBytes.size() - header.reflectionOffset;
  std::set<std::pair<uint64_t, uint64_t>, KeyPairLess> storedKeys;
  size_t poolViolations = 0;
  for (uint32_t pool = 0; pool < 2; pool++) {
    bool isVertexPool = pool == 0;
    auto count = isVertexPool ? header.vertPoolCount : header.fragPoolCount;
    auto poolOffset = isVertexPool ? header.vertPoolOffset : header.fragPoolOffset;
    for (uint32_t i = 0; i < count; i++) {
      auto entryOffset = poolOffset + static_cast<size_t>(i) * POOL_ENTRY_SIZE;
      if (entryOffset + POOL_ENTRY_SIZE > fileBytes.size()) {
        std::cout << "[verify] " << expectedTag << ": VIOLATION "
                  << (isVertexPool ? "vert" : "frag") << " pool entry " << i
                  << " lies outside the file (offset " << entryOffset << ", file "
                  << fileBytes.size() << " bytes)\n";
        poolViolations++;
        continue;
      }
      PoolEntryOnDisk entry;
      entry.hash.hi = ReadU64(fileBytes, entryOffset);
      entry.hash.lo = ReadU64(fileBytes, entryOffset + 8);
      entry.dataOffset = ReadU32(fileBytes, entryOffset + 16);
      entry.dataSize = ReadU32(fileBytes, entryOffset + 20);
      entry.reflOffset = ReadU32(fileBytes, entryOffset + 24);
      auto key = std::make_pair(entry.hash.hi, entry.hash.lo);
      if (!storedKeys.insert(key).second) {
        std::cout << "[verify] " << expectedTag << ": VIOLATION "
                  << (isVertexPool ? "vert" : "frag") << " entry " << i << " key "
                  << FormatKeyHex(key) << " appears more than once in the pool\n";
        poolViolations++;
      }
      auto candidate = expected.find(key);
      if (candidate == expected.end()) {
        std::cout << "[verify] " << expectedTag << ": VIOLATION "
                  << (isVertexPool ? "vert" : "frag") << " entry " << i << " key "
                  << FormatKeyHex(key) << " has no rule-reachable candidate\n";
        poolViolations++;
      } else if (candidate->second.size() > 1) {
        std::cout << "[verify] " << expectedTag << ": VIOLATION "
                  << (isVertexPool ? "vert" : "frag") << " entry " << i << " key "
                  << FormatKeyHex(key) << " maps to multiple candidates:";
        for (const auto& label : candidate->second) {
          std::cout << " " << label;
        }
        std::cout << "\n";
        poolViolations++;
      }
      if (static_cast<size_t>(entry.dataOffset) + entry.dataSize > dataPool.size()) {
        std::cout << "[verify] " << expectedTag << ": VIOLATION "
                  << (isVertexPool ? "vert" : "frag") << " entry " << i << " data range ["
                  << entry.dataOffset << ", "
                  << static_cast<size_t>(entry.dataOffset) + entry.dataSize
                  << ") exceeds the data pool (" << dataPool.size() << " bytes)\n";
        poolViolations++;
      }
      if (static_cast<size_t>(entry.reflOffset) + 4 > reflPoolSize) {
        std::cout << "[verify] " << expectedTag << ": VIOLATION "
                  << (isVertexPool ? "vert" : "frag") << " entry " << i << " reflection offset "
                  << entry.reflOffset << " outside the reflection pool\n";
        poolViolations++;
      }
    }
  }
  result.violations += poolViolations;

  size_t missing = 0;
  for (const auto& entry : expected) {
    if (storedKeys.find(entry.first) == storedKeys.end()) {
      std::cout << "[verify] " << expectedTag
                << ": VIOLATION expected entry missing from bundle: " << FormatKeyHex(entry.first);
      for (const auto& label : entry.second) {
        std::cout << " " << label;
      }
      std::cout << "\n";
      missing++;
    }
  }
  result.violations += missing;

  // Identity hash recomputation over the same field sequence the writer fed: header fields,
  // pool entries, the uncompressed data pool, and the reflection pool. A mismatch means the
  // file was modified or truncated after it was written.
  char tagBuf[32] = {};
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
      auto entryOffset = poolOffset + static_cast<size_t>(i) * POOL_ENTRY_SIZE;
      identity = BundleIdentityHashEntry(
          identity, ReadU64(fileBytes, entryOffset), ReadU64(fileBytes, entryOffset + 8),
          ReadU32(fileBytes, entryOffset + 16), ReadU32(fileBytes, entryOffset + 20),
          ReadU32(fileBytes, entryOffset + 24));
    }
  }
  identity = BundleIdentityHashBytes(identity, dataPool.data(), dataPool.size());
  identity = BundleIdentityHashBytes(identity, fileBytes.data() + header.reflectionOffset,
                                     fileBytes.size() - header.reflectionOffset);
  if (identity != header.identityHash) {
    std::cout << "[verify] " << expectedTag << ": VIOLATION identity hash mismatch (stored 0x"
              << std::hex << header.identityHash << ", recomputed 0x" << identity << std::dec
              << ")\n";
    result.violations++;
  }

  std::cout << "[verify] " << expectedTag << ": " << (result.violations == 0 ? "OK" : "FAILED")
            << " (" << header.vertPoolCount << " vert + " << header.fragPoolCount
            << " frag entries, " << (poolViolations + missing) << " pool violations)\n";
  return result;
}

}  // namespace

int VerifyBundles(const std::string& bundleDir) {
  static const char* BACKENDS[] = {"opengl", "opengles", "vulkan", "metal", "webgpu"};
  int totalViolations = 0;
  size_t openedBundles = 0;
  for (const auto* backend : BACKENDS) {
    auto path = bundleDir + "/shader_bundle." + backend + ".bin";
    auto result = VerifyOneBundle(path, backend);
    if (result.opened) {
      openedBundles++;
    }
    totalViolations += static_cast<int>(result.violations);
  }
  if (openedBundles == 0) {
    std::cerr << "[verify] no bundles found in " << bundleDir << "\n";
    return 1;
  }
  std::cout << "[verify] summary: " << openedBundles << " bundle(s), " << totalViolations
            << " violation(s)\n";
  return totalViolations == 0 ? 0 : 1;
}

}  // namespace tgfx
