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
  // Write shaderName: length prefix (4 bytes LE) + content
  auto nameLen = static_cast<uint32_t>(shaderName.size());
  bytes.push_back(static_cast<uint8_t>(nameLen & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 24) & 0xFF));
  bytes.insert(bytes.end(), shaderName.begin(), shaderName.end());
  // Write permutationIndex (4 bytes LE)
  bytes.push_back(static_cast<uint8_t>(permutationIndex & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 24) & 0xFF));
  // Write profileTag: length prefix (4 bytes LE) + content
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

// Serializes a single ReflectionData into a byte blob.
// Format: [vertexUniformCount:u8][fragmentUniformCount:u8][samplerCount:u8][reserved:u8]
//         For each uniform: [nameLen:u8][name:bytes][format:u8]
//         For each sampler: [nameLen:u8][name:bytes][format:u8]
static std::vector<uint8_t> SerializeReflection(const ReflectionData& reflection) {
  std::vector<uint8_t> blob;
  blob.push_back(static_cast<uint8_t>(reflection.vertexUniforms.size()));
  blob.push_back(static_cast<uint8_t>(reflection.fragmentUniforms.size()));
  blob.push_back(static_cast<uint8_t>(reflection.samplers.size()));
  blob.push_back(0);  // reserved padding

  auto writeEntries = [&blob](const std::vector<UniformEntry>& entries) {
    for (const auto& entry : entries) {
      auto nameLen = static_cast<uint8_t>(entry.name.size());
      blob.push_back(nameLen);
      blob.insert(blob.end(), entry.name.begin(), entry.name.end());
      blob.push_back(entry.format);
    }
  };

  writeEntries(reflection.vertexUniforms);
  writeEntries(reflection.fragmentUniforms);
  writeEntries(reflection.samplers);
  return blob;
}

bool WriteBundle(const std::string& outPath, const std::string& profileTag,
                 const std::vector<VariantData>& variants) {
  if (variants.empty()) {
    return true;
  }

  // Compute hashes and build index entries
  struct IndexedEntry {
    ShaderKeyHash hash;
    size_t variantIdx;
  };
  std::vector<IndexedEntry> entries;
  entries.reserve(variants.size());
  for (size_t i = 0; i < variants.size(); i++) {
    const auto& v = variants[i];
    auto hash = ComputeShaderKeyHash(v.shaderName, v.permutationIndex, v.profileTag);
    entries.push_back({hash, i});
  }

  // Sort by hash (hi, lo) for binary search at runtime
  std::sort(entries.begin(), entries.end(), [](const IndexedEntry& a, const IndexedEntry& b) {
    if (a.hash.hi != b.hash.hi) {
      return a.hash.hi < b.hash.hi;
    }
    return a.hash.lo < b.hash.lo;
  });

  // Check for duplicates
  for (size_t i = 1; i < entries.size(); i++) {
    if (entries[i].hash.hi == entries[i - 1].hash.hi &&
        entries[i].hash.lo == entries[i - 1].hash.lo) {
      std::cerr << "ERROR: Duplicate hash in bundle for variant "
                << variants[entries[i].variantIdx].shaderName << " index "
                << variants[entries[i].variantIdx].permutationIndex << "\n";
      return false;
    }
  }

  // Layout: FileHeader | IndexEntries[] | ShaderDataPool | ReflectionPool
  uint32_t headerSize = sizeof(BundleFileHeader);
  uint32_t indexSize = static_cast<uint32_t>(entries.size()) * sizeof(BundleIndexEntry);
  uint32_t shaderDataStart = headerSize + indexSize;

  // Build shader data pool and reflection pool, record offsets
  std::vector<uint8_t> shaderDataPool;
  std::vector<uint8_t> reflectionPool;
  std::vector<BundleIndexEntry> indexEntries(entries.size());

  for (size_t i = 0; i < entries.size(); i++) {
    const auto& v = variants[entries[i].variantIdx];
    auto& idx = indexEntries[i];
    idx.shaderKeyHashHi = entries[i].hash.hi;
    idx.shaderKeyHashLo = entries[i].hash.lo;
    idx.vertexBlobOffset = static_cast<uint32_t>(shaderDataPool.size());
    idx.vertexBlobSize = static_cast<uint32_t>(v.vertexBlob.size());
    shaderDataPool.insert(shaderDataPool.end(), v.vertexBlob.begin(), v.vertexBlob.end());
    idx.fragmentBlobOffset = static_cast<uint32_t>(shaderDataPool.size());
    idx.fragmentBlobSize = static_cast<uint32_t>(v.fragmentBlob.size());
    shaderDataPool.insert(shaderDataPool.end(), v.fragmentBlob.begin(), v.fragmentBlob.end());

    // Serialize reflection for this entry
    auto reflBlob = SerializeReflection(v.reflection);
    idx.reflectionOffset = static_cast<uint32_t>(reflectionPool.size());
    reflectionPool.insert(reflectionPool.end(), reflBlob.begin(), reflBlob.end());
  }

  // Write file
  std::ofstream file(outPath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "ERROR: Cannot open output file: " << outPath << "\n";
    return false;
  }

  // Compute reflection pool file offset
  uint32_t reflectionPoolFileOffset =
      shaderDataStart + static_cast<uint32_t>(shaderDataPool.size());

  // Write header
  BundleFileHeader header;
  header.formatVersion = 2;
  header.entryCount = static_cast<uint32_t>(entries.size());
  header.shaderDataOffset = shaderDataStart;
  header.shaderDataSize = static_cast<uint32_t>(shaderDataPool.size());
  header.reflectionOffset = reflectionPool.empty() ? 0 : reflectionPoolFileOffset;
  std::strncpy(header.profileTag, profileTag.c_str(), sizeof(header.profileTag) - 1);

  WriteU32LE(file, header.magic);
  WriteU16LE(file, header.formatVersion);
  WriteU16LE(file, header.compressionType);
  WriteU64LE(file, header.sourceHash);
  WriteU32LE(file, header.toolchainVersion);
  WriteU32LE(file, header.entryCount);
  WriteU32LE(file, header.reflectionOffset);
  WriteU32LE(file, header.shaderDataOffset);
  WriteU32LE(file, header.shaderDataSize);
  file.write(header.profileTag, sizeof(header.profileTag));

  // Write index entries
  for (const auto& idx : indexEntries) {
    WriteU64LE(file, idx.shaderKeyHashHi);
    WriteU64LE(file, idx.shaderKeyHashLo);
    WriteU32LE(file, idx.vertexBlobOffset);
    WriteU32LE(file, idx.vertexBlobSize);
    WriteU32LE(file, idx.fragmentBlobOffset);
    WriteU32LE(file, idx.fragmentBlobSize);
    WriteU32LE(file, idx.reflectionOffset);
  }

  // Write shader data pool
  file.write(reinterpret_cast<const char*>(shaderDataPool.data()),
             static_cast<std::streamsize>(shaderDataPool.size()));

  // Write reflection pool
  if (!reflectionPool.empty()) {
    file.write(reinterpret_cast<const char*>(reflectionPool.data()),
               static_cast<std::streamsize>(reflectionPool.size()));
  }

  file.close();
  return true;
}

}  // namespace tgfx
