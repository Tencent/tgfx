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

// Must match BundleWriter's header layout (explicit LE serialization).
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

// FileHeader size: magic(4) + formatVersion(2) + compressionType(2) + sourceHash(8) +
//                  toolchainVersion(4) + entryCount(4) + reflectionOffset(4) +
//                  shaderDataOffset(4) + shaderDataSize(4) + profileTag(32) = 68 bytes
static constexpr size_t HEADER_SIZE = 68;
// IndexEntry size: hashHi(8) + hashLo(8) + vertOff(4) + vertSize(4) + fragOff(4) + fragSize(4) +
//                  reflOff(4) = 36 bytes
static constexpr size_t INDEX_ENTRY_SIZE = 36;

// Deserializes reflection data from the bundle's reflection pool into a ShaderBlob's
// vertexUniforms, fragmentUniforms and samplers fields.
// Format: [vertexUniformCount:u8][fragmentUniformCount:u8][samplerCount:u8][reserved:u8]
//         For each entry: [nameLen:u8][name:bytes][format:u8]
static bool ParseReflection(const uint8_t* data, size_t maxLen, ShaderBlob* blob) {
  if (maxLen < 4) {
    return false;
  }
  size_t offset = 0;
  uint8_t vertexCount = data[offset++];
  uint8_t fragmentCount = data[offset++];
  uint8_t samplerCount = data[offset++];
  offset++;  // reserved

  auto readEntries = [&](uint8_t count, std::vector<Uniform>& out) -> bool {
    for (uint8_t i = 0; i < count; i++) {
      if (offset >= maxLen) {
        return false;
      }
      uint8_t nameLen = data[offset++];
      if (offset + nameLen + 1 > maxLen) {
        return false;
      }
      std::string name(reinterpret_cast<const char*>(data + offset), nameLen);
      offset += nameLen;
      auto format = static_cast<UniformFormat>(data[offset++]);
      out.emplace_back(std::move(name), format);
    }
    return true;
  };

  if (!readEntries(vertexCount, blob->vertexUniforms)) {
    return false;
  }
  if (!readEntries(fragmentCount, blob->fragmentUniforms)) {
    return false;
  }
  if (!readEntries(samplerCount, blob->samplers)) {
    return false;
  }
  return true;
}

bool PrecompiledShaderCache::loadBundle(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }
  auto fileSize = static_cast<size_t>(file.tellg());
  if (fileSize < HEADER_SIZE) {
    LOGE("PrecompiledShaderCache: Bundle too small: %s", path.c_str());
    return false;
  }
  file.seekg(0);
  std::vector<uint8_t> data(fileSize);
  file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
  file.close();

  // Parse header
  const uint8_t* ptr = data.data();
  uint32_t magic = ReadU32LE(ptr);
  if (magic != 0x54475346) {  // "TGSF"
    LOGE("PrecompiledShaderCache: Invalid magic in %s", path.c_str());
    return false;
  }
  uint16_t formatVersion = ReadU16LE(ptr + 4);
  uint16_t compressionType = ReadU16LE(ptr + 6);
  if (compressionType != 0) {
    LOGE("PrecompiledShaderCache: Compressed bundles not yet supported");
    return false;
  }
  uint32_t entryCount = ReadU32LE(ptr + 20);
  uint32_t reflectionOffset = ReadU32LE(ptr + 24);
  uint32_t shaderDataOffset = ReadU32LE(ptr + 28);

  // Parse profileTag (32 bytes at offset 36)
  const char* tagPtr = reinterpret_cast<const char*>(ptr + 36);
  _profileTag = std::string(tagPtr, strnlen(tagPtr, 32));

  // Validate sizes
  size_t expectedMinSize = HEADER_SIZE + static_cast<size_t>(entryCount) * INDEX_ENTRY_SIZE;
  if (fileSize < expectedMinSize) {
    LOGE("PrecompiledShaderCache: File too small for %u entries", entryCount);
    return false;
  }

  bool hasReflection = (formatVersion >= 2 && reflectionOffset != 0);

  // Parse index entries
  const uint8_t* indexStart = ptr + HEADER_SIZE;
  for (uint32_t i = 0; i < entryCount; i++) {
    const uint8_t* entry = indexStart + static_cast<size_t>(i) * INDEX_ENTRY_SIZE;
    uint64_t hashHi = ReadU64LE(entry);
    uint64_t hashLo = ReadU64LE(entry + 8);
    uint32_t vertOff = ReadU32LE(entry + 16);
    uint32_t vertSize = ReadU32LE(entry + 20);
    uint32_t fragOff = ReadU32LE(entry + 24);
    uint32_t fragSize = ReadU32LE(entry + 28);
    uint32_t reflOff = ReadU32LE(entry + 32);

    // Offsets are relative to shader data pool start
    size_t vertAbsOff = shaderDataOffset + vertOff;
    size_t fragAbsOff = shaderDataOffset + fragOff;
    if (vertAbsOff + vertSize > fileSize || fragAbsOff + fragSize > fileSize) {
      LOGE("PrecompiledShaderCache: Entry %u has out-of-bounds offsets", i);
      return false;
    }

    ShaderBlob blob;
    blob.vertexData.assign(ptr + vertAbsOff, ptr + vertAbsOff + vertSize);
    blob.fragmentData.assign(ptr + fragAbsOff, ptr + fragAbsOff + fragSize);

    // Parse reflection data if available
    if (hasReflection) {
      size_t reflAbsOff = static_cast<size_t>(reflectionOffset) + reflOff;
      if (reflAbsOff < fileSize) {
        size_t maxReflLen = fileSize - reflAbsOff;
        if (!ParseReflection(ptr + reflAbsOff, maxReflLen, &blob)) {
          LOGE("PrecompiledShaderCache: Failed to parse reflection for entry %u", i);
          return false;
        }
      }
    }

    HashKey key{hashHi, hashLo};
    entries[key] = std::move(blob);
  }

  LOGI("PrecompiledShaderCache: Loaded %u entries from %s (format v%u%s)", entryCount, path.c_str(),
       formatVersion, hasReflection ? ", with reflection" : "");
  return true;
}

const ShaderBlob* PrecompiledShaderCache::find(uint64_t hashHi, uint64_t hashLo) const {
  HashKey key{hashHi, hashLo};
  auto it = entries.find(key);
  if (it == entries.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace tgfx
