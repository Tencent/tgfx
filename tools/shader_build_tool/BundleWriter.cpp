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

#include "BundleWriter.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include "gpu/PrecompiledBundleIdentity.h"
#include "zlib.h"
#include "zstd.h"

namespace tgfx {

static void WriteU16LE(std::ofstream& out, uint16_t val) {
  uint8_t buf[2] = {static_cast<uint8_t>(val & 0xFF), static_cast<uint8_t>((val >> 8) & 0xFF)};
  out.write(reinterpret_cast<char*>(buf), 2);
}

static void WriteU32LE(std::ofstream& out, uint32_t val) {
  uint8_t buf[4];
  buf[0] = static_cast<uint8_t>(val & 0xFF);
  buf[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
  buf[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
  buf[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
  out.write(reinterpret_cast<char*>(buf), 4);
}

static void WriteU64LE(std::ofstream& out, uint64_t val) {
  uint8_t buf[8];
  for (int i = 0; i < 8; i++) {
    buf[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
  }
  out.write(reinterpret_cast<char*>(buf), 8);
}

static void WriteUniformEntries(std::vector<uint8_t>& blob,
                                const std::vector<UniformEntry>& entries) {
  for (const auto& entry : entries) {
    auto nameLen = static_cast<uint8_t>(entry.name.size());
    blob.push_back(nameLen);
    blob.insert(blob.end(), entry.name.begin(), entry.name.end());
    blob.push_back(entry.format);
    // v4: array element count (1 for scalars).
    blob.push_back(static_cast<uint8_t>(entry.arraySize & 0xFF));
    blob.push_back(static_cast<uint8_t>((entry.arraySize >> 8) & 0xFF));
  }
}

std::vector<uint8_t> SerializeStageReflection(const StageReflectionData& reflection) {
  std::vector<uint8_t> blob;
  blob.push_back(static_cast<uint8_t>(reflection.uniforms.size()));
  blob.push_back(static_cast<uint8_t>(reflection.samplers.size()));
  blob.push_back(0);  // reserved
  blob.push_back(0);  // reserved

  WriteUniformEntries(blob, reflection.uniforms);
  WriteUniformEntries(blob, reflection.samplers);
  return blob;
}

struct PoolEntry {
  ShaderKeyHash hash;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  uint32_t reflOffset = 0;
};

// v3 Header layout (80 bytes):
//   magic(4) + formatVersion(2) + compressionType(2) + sourceHash(8) + toolchainVersion(4) +
//   vertPoolCount(4) + fragPoolCount(4) + vertPoolOffset(4) + fragPoolOffset(4) +
//   dataOffset(4) + dataSize(4) + reflectionOffset(4) + profileTag(32) = 80
static constexpr uint32_t HEADER_SIZE_V3 = 80;
// PoolEntry on disk: hashHi(8) + hashLo(8) + dataOff(4) + dataSize(4) + reflOff(4) = 28
static constexpr uint32_t POOL_ENTRY_SIZE = 28;

bool WriteBundle(const std::string& outPath, const std::string& profileTag,
                 const std::vector<VariantData>& variants, bool compress) {
  if (variants.empty()) {
    return true;
  }

  // Build separate vert and frag entries with deduplication by hash.
  std::vector<uint8_t> dataPool;
  std::vector<uint8_t> reflPool;
  std::vector<PoolEntry> vertPool;
  std::vector<PoolEntry> fragPool;

  struct HashKeyLess {
    bool operator()(const ShaderKeyHash& a, const ShaderKeyHash& b) const {
      if (a.hi != b.hi) {
        return a.hi < b.hi;
      }
      return a.lo < b.lo;
    }
  };
  // A repeated stage key is only legal when the content is byte-identical (true deduplication);
  // two different code or reflection blobs under one key would silently drop one of them — the
  // first here, the last in the runtime loader — so the collision itself must fail the build.
  struct SeenEntry {
    std::vector<uint8_t> code;
    std::vector<uint8_t> reflection;
  };
  std::map<ShaderKeyHash, SeenEntry, HashKeyLess> vertSeen;
  std::map<ShaderKeyHash, SeenEntry, HashKeyLess> fragSeen;

  for (const auto& v : variants) {
    // Vertex entry: key = hash(shaderName + "_Vert", vertPermutationIndex, profileTag)
    auto vertHash =
        ComputeShaderKeyHash(v.shaderName + "_Vert", v.vertPermutationIndex, v.profileTag);
    auto vertReflBlob = SerializeStageReflection(v.vertexReflection);
    auto vertIt = vertSeen.find(vertHash);
    if (vertIt == vertSeen.end()) {
      vertSeen.emplace(vertHash, SeenEntry{v.vertexBlob, vertReflBlob});
      PoolEntry entry;
      entry.hash = vertHash;
      entry.dataOffset = static_cast<uint32_t>(dataPool.size());
      entry.dataSize = static_cast<uint32_t>(v.vertexBlob.size());
      dataPool.insert(dataPool.end(), v.vertexBlob.begin(), v.vertexBlob.end());
      entry.reflOffset = static_cast<uint32_t>(reflPool.size());
      reflPool.insert(reflPool.end(), vertReflBlob.begin(), vertReflBlob.end());
      vertPool.push_back(entry);
    } else if (vertIt->second.code != v.vertexBlob || vertIt->second.reflection != vertReflBlob) {
      std::cerr << "BundleWriter: conflicting content for duplicate vertex key (shader "
                << v.shaderName << ", vert index " << v.vertPermutationIndex << ", profile "
                << v.profileTag << "); refusing to silently drop either variant\n";
      return false;
    }

    // Fragment entry: key = hash(shaderName + "_Frag", fragPermutationIndex, profileTag)
    auto fragHash =
        ComputeShaderKeyHash(v.shaderName + "_Frag", v.fragPermutationIndex, v.profileTag);
    auto fragReflBlob = SerializeStageReflection(v.fragmentReflection);
    auto fragIt = fragSeen.find(fragHash);
    if (fragIt == fragSeen.end()) {
      fragSeen.emplace(fragHash, SeenEntry{v.fragmentBlob, fragReflBlob});
      PoolEntry entry;
      entry.hash = fragHash;
      entry.dataOffset = static_cast<uint32_t>(dataPool.size());
      entry.dataSize = static_cast<uint32_t>(v.fragmentBlob.size());
      dataPool.insert(dataPool.end(), v.fragmentBlob.begin(), v.fragmentBlob.end());
      entry.reflOffset = static_cast<uint32_t>(reflPool.size());
      reflPool.insert(reflPool.end(), fragReflBlob.begin(), fragReflBlob.end());
      fragPool.push_back(entry);
    } else if (fragIt->second.code != v.fragmentBlob || fragIt->second.reflection != fragReflBlob) {
      std::cerr << "BundleWriter: conflicting content for duplicate fragment key (shader "
                << v.shaderName << ", frag index " << v.fragPermutationIndex << ", profile "
                << v.profileTag << "); refusing to silently drop either variant\n";
      return false;
    }
  }

  // Sort pools by hash for deterministic output
  auto hashCompare = [](const PoolEntry& a, const PoolEntry& b) {
    if (a.hash.hi != b.hash.hi) {
      return a.hash.hi < b.hash.hi;
    }
    return a.hash.lo < b.hash.lo;
  };
  std::sort(vertPool.begin(), vertPool.end(), hashCompare);
  std::sort(fragPool.begin(), fragPool.end(), hashCompare);

  // Compute layout offsets
  auto vertPoolCount = static_cast<uint32_t>(vertPool.size());
  auto fragPoolCount = static_cast<uint32_t>(fragPool.size());
  uint32_t vertPoolOffset = HEADER_SIZE_V3;
  uint32_t fragPoolOffset = vertPoolOffset + vertPoolCount * POOL_ENTRY_SIZE;
  uint32_t dataOffset = fragPoolOffset + fragPoolCount * POOL_ENTRY_SIZE;
  uint32_t dataSize = static_cast<uint32_t>(dataPool.size());

  // Write file
  std::ofstream file(outPath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "ERROR: Cannot open output file: " << outPath << "\n";
    return false;
  }

  // Optionally compress the data pool with zstd (compressionType=2; 1 is the legacy zlib path).
  std::vector<uint8_t> compressedData;
  uint16_t compressionFlag = 0;
  const uint8_t* dataToWrite = dataPool.data();
  size_t dataToWriteSize = dataPool.size();
  if (compress && !dataPool.empty()) {
    size_t compBound = ZSTD_compressBound(dataPool.size());
    compressedData.resize(compBound);
    size_t compSize = ZSTD_compress(compressedData.data(), compBound, dataPool.data(),
                                    dataPool.size(), ZSTD_maxCLevel());
    if (!ZSTD_isError(compSize) && compSize < dataPool.size()) {
      compressedData.resize(compSize);
      dataToWrite = compressedData.data();
      dataToWriteSize = compSize;
      compressionFlag = 2;
    }
    // If compression didn't reduce size, fall back to uncompressed.
  }

  // Recompute reflectionOffset based on actual data written.
  uint32_t actualReflectionOffset =
      reflPool.empty() ? 0 : static_cast<uint32_t>(dataOffset + dataToWriteSize);

  // Identity hash: covers the serialized pool entries, the uncompressed data and reflection
  // pools, the profile tag, and the counts. The runtime loader recomputes this over the same
  // fields and rejects a bundle whose hash does not match, closing the gap where a same-name
  // bundle with changed shader content or reflection layout would otherwise load fine.
  char tagBuf[32] = {};
  std::strncpy(tagBuf, profileTag.c_str(), sizeof(tagBuf) - 1);
  uint64_t identityHash = BundleIdentityHashInit();
  identityHash = BundleIdentityHashHeader(identityHash, 4, vertPoolCount, fragPoolCount,
                                          reinterpret_cast<const uint8_t*>(tagBuf));
  for (const auto& entry : vertPool) {
    identityHash = BundleIdentityHashEntry(identityHash, entry.hash.hi, entry.hash.lo,
                                           entry.dataOffset, entry.dataSize, entry.reflOffset);
  }
  for (const auto& entry : fragPool) {
    identityHash = BundleIdentityHashEntry(identityHash, entry.hash.hi, entry.hash.lo,
                                           entry.dataOffset, entry.dataSize, entry.reflOffset);
  }
  identityHash = BundleIdentityHashBytes(identityHash, dataPool.data(), dataPool.size());
  identityHash = BundleIdentityHashBytes(identityHash, reflPool.data(), reflPool.size());

  // Header
  WriteU32LE(file, 0x54475346);              // magic "TGSF"
  WriteU16LE(file, 4);                       // formatVersion
  WriteU16LE(file, compressionFlag);         // compressionType
  WriteU64LE(file, identityHash);            // sourceHash (bundle identity, see above)
  WriteU32LE(file, kExpectedToolchainABI);   // toolchain ABI (see PrecompiledBundleIdentity.h)
  WriteU32LE(file, vertPoolCount);           // vertPoolCount
  WriteU32LE(file, fragPoolCount);           // fragPoolCount
  WriteU32LE(file, vertPoolOffset);          // vertPoolOffset
  WriteU32LE(file, fragPoolOffset);          // fragPoolOffset
  WriteU32LE(file, dataOffset);              // dataOffset
  WriteU32LE(file, dataSize);                // dataSize (uncompressed)
  WriteU32LE(file, actualReflectionOffset);  // reflectionOffset
  file.write(tagBuf, sizeof(tagBuf));

  // Vert pool entries
  for (const auto& entry : vertPool) {
    WriteU64LE(file, entry.hash.hi);
    WriteU64LE(file, entry.hash.lo);
    WriteU32LE(file, entry.dataOffset);
    WriteU32LE(file, entry.dataSize);
    WriteU32LE(file, entry.reflOffset);
  }

  // Frag pool entries
  for (const auto& entry : fragPool) {
    WriteU64LE(file, entry.hash.hi);
    WriteU64LE(file, entry.hash.lo);
    WriteU32LE(file, entry.dataOffset);
    WriteU32LE(file, entry.dataSize);
    WriteU32LE(file, entry.reflOffset);
  }

  // Data pool (possibly compressed)
  file.write(reinterpret_cast<const char*>(dataToWrite),
             static_cast<std::streamsize>(dataToWriteSize));

  // Reflection pool
  if (!reflPool.empty()) {
    file.write(reinterpret_cast<const char*>(reflPool.data()),
               static_cast<std::streamsize>(reflPool.size()));
  }

  // A failed stream (disk full, permission) must not report success: the caller would treat the
  // truncated or missing file as a valid bundle.
  if (!file.good()) {
    std::cerr << "BundleWriter: stream error while writing bundle (disk full or I/O failure)\n";
    file.close();
    return false;
  }
  file.close();
  return true;
}

}  // namespace tgfx
