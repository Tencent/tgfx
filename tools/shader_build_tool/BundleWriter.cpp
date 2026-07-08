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
#include <set>

namespace tgfx {

static constexpr uint64_t FNV1A_SEED_HI = 0x6C62272E07BB0142ULL;
static constexpr uint64_t FNV1A_SEED_LO = 0x62B821756295C58DULL;
static constexpr uint64_t FNV1A_PRIME = 0x00000100000001B3ULL;

static uint64_t Fnv1a64(const std::vector<uint8_t>& data, uint64_t seed) {
  uint64_t hash = seed;
  for (auto byte : data) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= FNV1A_PRIME;
  }
  return hash;
}

static std::vector<uint8_t> SerializeFieldsLE(const std::string& shaderName,
                                              uint32_t permutationIndex,
                                              const std::string& profileTag) {
  std::vector<uint8_t> bytes;
  auto nameLen = static_cast<uint32_t>(shaderName.size());
  bytes.push_back(static_cast<uint8_t>(nameLen & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 24) & 0xFF));
  bytes.insert(bytes.end(), shaderName.begin(), shaderName.end());
  bytes.push_back(static_cast<uint8_t>(permutationIndex & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 24) & 0xFF));
  auto tagLen = static_cast<uint32_t>(profileTag.size());
  bytes.push_back(static_cast<uint8_t>(tagLen & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 24) & 0xFF));
  bytes.insert(bytes.end(), profileTag.begin(), profileTag.end());
  return bytes;
}

ShaderKeyHash ComputeShaderKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                   const std::string& profileTag) {
  auto data = SerializeFieldsLE(shaderName, permutationIndex, profileTag);
  ShaderKeyHash result;
  result.hi = Fnv1a64(data, FNV1A_SEED_HI);
  result.lo = Fnv1a64(data, FNV1A_SEED_LO);
  return result;
}

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
  }
}

// Serializes stage reflection: [uniformCount:u8][samplerCount:u8][reserved:u8][reserved:u8]
//                              For each: [nameLen:u8][name:bytes][format:u8]
static std::vector<uint8_t> SerializeStageReflection(const StageReflectionData& reflection) {
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
                 const std::vector<VariantData>& variants) {
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
  std::set<ShaderKeyHash, HashKeyLess> vertSeen;
  std::set<ShaderKeyHash, HashKeyLess> fragSeen;

  for (const auto& v : variants) {
    // Vertex entry: key = hash(shaderName + "_Vert", vertPermutationIndex, profileTag)
    auto vertHash =
        ComputeShaderKeyHash(v.shaderName + "_Vert", v.vertPermutationIndex, v.profileTag);
    if (vertSeen.find(vertHash) == vertSeen.end()) {
      vertSeen.insert(vertHash);
      PoolEntry entry;
      entry.hash = vertHash;
      entry.dataOffset = static_cast<uint32_t>(dataPool.size());
      entry.dataSize = static_cast<uint32_t>(v.vertexBlob.size());
      dataPool.insert(dataPool.end(), v.vertexBlob.begin(), v.vertexBlob.end());
      auto reflBlob = SerializeStageReflection(v.vertexReflection);
      entry.reflOffset = static_cast<uint32_t>(reflPool.size());
      reflPool.insert(reflPool.end(), reflBlob.begin(), reflBlob.end());
      vertPool.push_back(entry);
    }

    // Fragment entry: key = hash(shaderName + "_Frag", fragPermutationIndex, profileTag)
    auto fragHash =
        ComputeShaderKeyHash(v.shaderName + "_Frag", v.fragPermutationIndex, v.profileTag);
    if (fragSeen.find(fragHash) == fragSeen.end()) {
      fragSeen.insert(fragHash);
      PoolEntry entry;
      entry.hash = fragHash;
      entry.dataOffset = static_cast<uint32_t>(dataPool.size());
      entry.dataSize = static_cast<uint32_t>(v.fragmentBlob.size());
      dataPool.insert(dataPool.end(), v.fragmentBlob.begin(), v.fragmentBlob.end());
      auto reflBlob = SerializeStageReflection(v.fragmentReflection);
      entry.reflOffset = static_cast<uint32_t>(reflPool.size());
      reflPool.insert(reflPool.end(), reflBlob.begin(), reflBlob.end());
      fragPool.push_back(entry);
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
  uint32_t reflectionOffset = dataOffset + dataSize;

  // Write file
  std::ofstream file(outPath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "ERROR: Cannot open output file: " << outPath << "\n";
    return false;
  }

  // Header
  WriteU32LE(file, 0x54475346);       // magic "TGSF"
  WriteU16LE(file, 3);                // formatVersion
  WriteU16LE(file, 0);                // compressionType
  WriteU64LE(file, 0);                // sourceHash (reserved)
  WriteU32LE(file, 0x00010000);       // toolchainVersion 1.0.0
  WriteU32LE(file, vertPoolCount);    // vertPoolCount
  WriteU32LE(file, fragPoolCount);    // fragPoolCount
  WriteU32LE(file, vertPoolOffset);   // vertPoolOffset
  WriteU32LE(file, fragPoolOffset);   // fragPoolOffset
  WriteU32LE(file, dataOffset);       // dataOffset
  WriteU32LE(file, dataSize);         // dataSize
  WriteU32LE(file, reflPool.empty() ? 0 : reflectionOffset);  // reflectionOffset
  char tagBuf[32] = {};
  std::strncpy(tagBuf, profileTag.c_str(), sizeof(tagBuf) - 1);
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

  // Data pool
  file.write(reinterpret_cast<const char*>(dataPool.data()),
             static_cast<std::streamsize>(dataPool.size()));

  // Reflection pool
  if (!reflPool.empty()) {
    file.write(reinterpret_cast<const char*>(reflPool.data()),
               static_cast<std::streamsize>(reflPool.size()));
  }

  file.close();
  return true;
}

}  // namespace tgfx
