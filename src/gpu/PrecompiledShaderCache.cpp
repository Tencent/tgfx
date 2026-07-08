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

#include "PrecompiledShaderCache.h"
#include <cstring>
#include <fstream>
#include "core/utils/Log.h"

namespace tgfx {

static uint16_t ReadU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

static uint32_t ReadU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t ReadU64LE(const uint8_t* p) {
  uint64_t val = 0;
  for (int i = 0; i < 8; i++) {
    val |= static_cast<uint64_t>(p[i]) << (i * 8);
  }
  return val;
}

// v3 FileHeader: magic(4) + formatVersion(2) + compressionType(2) + sourceHash(8) +
//   toolchainVersion(4) + vertPoolCount(4) + fragPoolCount(4) + vertPoolOffset(4) +
//   fragPoolOffset(4) + dataOffset(4) + dataSize(4) + reflectionOffset(4) + profileTag(32) = 80
static constexpr size_t HEADER_SIZE_V3 = 80;
// PoolEntry: hashHi(8) + hashLo(8) + dataOff(4) + dataSize(4) + reflOff(4) = 28
static constexpr size_t POOL_ENTRY_SIZE = 28;

static bool ReadUniformEntries(const uint8_t* data, size_t maxLen, size_t* offset, uint8_t count,
                               std::vector<Uniform>& out) {
  for (uint8_t i = 0; i < count; i++) {
    if (*offset >= maxLen) {
      return false;
    }
    uint8_t nameLen = data[(*offset)++];
    if (*offset + nameLen + 1 > maxLen) {
      return false;
    }
    std::string name(reinterpret_cast<const char*>(data + *offset), nameLen);
    *offset += nameLen;
    auto format = static_cast<UniformFormat>(data[(*offset)++]);
    out.emplace_back(std::move(name), format);
  }
  return true;
}

// Reflection format: [uniformCount:u8][samplerCount:u8][reserved:u8][reserved:u8]
//                    For each uniform: [nameLen:u8][name:bytes][format:u8]
//                    For each sampler: [nameLen:u8][name:bytes][format:u8]
static bool ParseStageReflection(const uint8_t* data, size_t maxLen, ShaderStageBlob* blob) {
  if (maxLen < 4) {
    return false;
  }
  size_t offset = 0;
  uint8_t uniformCount = data[offset++];
  uint8_t samplerCount = data[offset++];
  offset += 2;  // reserved

  if (!ReadUniformEntries(data, maxLen, &offset, uniformCount, blob->uniforms)) {
    return false;
  }
  if (!ReadUniformEntries(data, maxLen, &offset, samplerCount, blob->samplers)) {
    return false;
  }
  return true;
}

static bool LoadPool(const uint8_t* fileData, size_t fileSize, uint32_t poolOffset,
                     uint32_t poolCount, uint32_t dataOffset, uint32_t reflectionOffset,
                     std::unordered_map<PrecompiledShaderCache::HashKey, ShaderStageBlob,
                                        PrecompiledShaderCache::HashKeyHasher>& entries) {
  for (uint32_t i = 0; i < poolCount; i++) {
    size_t entryOff = poolOffset + static_cast<size_t>(i) * POOL_ENTRY_SIZE;
    if (entryOff + POOL_ENTRY_SIZE > fileSize) {
      LOGE("PrecompiledShaderCache: Pool entry %u out of bounds", i);
      return false;
    }
    const uint8_t* entry = fileData + entryOff;
    uint64_t hashHi = ReadU64LE(entry);
    uint64_t hashLo = ReadU64LE(entry + 8);
    uint32_t blobOff = ReadU32LE(entry + 16);
    uint32_t blobSize = ReadU32LE(entry + 20);
    uint32_t reflOff = ReadU32LE(entry + 24);

    size_t absDataOff = static_cast<size_t>(dataOffset) + blobOff;
    if (absDataOff + blobSize > fileSize) {
      LOGE("PrecompiledShaderCache: Data blob out of bounds for entry %u", i);
      return false;
    }

    ShaderStageBlob blob;
    blob.data.assign(fileData + absDataOff, fileData + absDataOff + blobSize);

    if (reflectionOffset != 0) {
      size_t absReflOff = static_cast<size_t>(reflectionOffset) + reflOff;
      if (absReflOff < fileSize) {
        size_t maxReflLen = fileSize - absReflOff;
        if (!ParseStageReflection(fileData + absReflOff, maxReflLen, &blob)) {
          LOGE("PrecompiledShaderCache: Failed to parse reflection for entry %u", i);
          return false;
        }
      }
    }

    PrecompiledShaderCache::HashKey key{hashHi, hashLo};
    entries[key] = std::move(blob);
  }
  return true;
}

bool PrecompiledShaderCache::loadBundle(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }
  auto fileSize = static_cast<size_t>(file.tellg());
  if (fileSize < HEADER_SIZE_V3) {
    LOGE("PrecompiledShaderCache: Bundle too small: %s", path.c_str());
    return false;
  }
  file.seekg(0);
  std::vector<uint8_t> data(fileSize);
  file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
  file.close();

  const uint8_t* ptr = data.data();
  uint32_t magic = ReadU32LE(ptr);
  if (magic != 0x54475346) {  // "TGSF"
    LOGE("PrecompiledShaderCache: Invalid magic in %s", path.c_str());
    return false;
  }
  uint16_t formatVersion = ReadU16LE(ptr + 4);
  if (formatVersion != 3) {
    LOGE("PrecompiledShaderCache: Unsupported format version %u (expected 3)", formatVersion);
    return false;
  }
  uint16_t compressionType = ReadU16LE(ptr + 6);
  if (compressionType != 0) {
    LOGE("PrecompiledShaderCache: Compressed bundles not yet supported");
    return false;
  }
  // offset 8: sourceHash(8), offset 16: toolchainVersion(4)
  uint32_t vertPoolCount = ReadU32LE(ptr + 20);
  uint32_t fragPoolCount = ReadU32LE(ptr + 24);
  uint32_t vertPoolOffset = ReadU32LE(ptr + 28);
  uint32_t fragPoolOffset = ReadU32LE(ptr + 32);
  uint32_t dataOffset = ReadU32LE(ptr + 36);
  uint32_t dataSize = ReadU32LE(ptr + 40);
  uint32_t reflectionOffset = ReadU32LE(ptr + 44);
  (void)dataSize;

  // Parse profileTag (32 bytes at offset 48)
  const char* tagPtr = reinterpret_cast<const char*>(ptr + 48);
  _profileTag = std::string(tagPtr, strnlen(tagPtr, 32));

  if (!LoadPool(ptr, fileSize, vertPoolOffset, vertPoolCount, dataOffset, reflectionOffset,
                vertEntries)) {
    return false;
  }
  if (!LoadPool(ptr, fileSize, fragPoolOffset, fragPoolCount, dataOffset, reflectionOffset,
                fragEntries)) {
    return false;
  }

  LOGI("PrecompiledShaderCache: Loaded %u vert + %u frag entries from %s (format v%u)",
       vertPoolCount, fragPoolCount, path.c_str(), formatVersion);
  return true;
}

const ShaderStageBlob* PrecompiledShaderCache::findVertex(uint64_t hashHi, uint64_t hashLo) const {
  HashKey key{hashHi, hashLo};
  auto it = vertEntries.find(key);
  if (it == vertEntries.end()) {
    return nullptr;
  }
  return &it->second;
}

const ShaderStageBlob* PrecompiledShaderCache::findFragment(uint64_t hashHi,
                                                            uint64_t hashLo) const {
  HashKey key{hashHi, hashLo};
  auto it = fragEntries.find(key);
  if (it == fragEntries.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace tgfx
