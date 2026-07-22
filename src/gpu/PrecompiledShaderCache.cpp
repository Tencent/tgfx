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
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include "core/utils/Log.h"
#include "zlib.h"

namespace tgfx {

const char* PrecompiledFallbackReasonName(PrecompiledFallbackReason reason) {
  switch (reason) {
    case PrecompiledFallbackReason::CacheNotLoaded:
      return "CacheNotLoaded";
    case PrecompiledFallbackReason::UnsupportedOutputSwizzle:
      return "UnsupportedOutputSwizzle";
    case PrecompiledFallbackReason::NoMatchingRule:
      return "NoMatchingRule";
    case PrecompiledFallbackReason::VertexArtifactMissing:
      return "VertexArtifactMissing";
    case PrecompiledFallbackReason::FragmentArtifactMissing:
      return "FragmentArtifactMissing";
    case PrecompiledFallbackReason::VertexModuleCreationFailed:
      return "VertexModuleCreationFailed";
    case PrecompiledFallbackReason::FragmentModuleCreationFailed:
      return "FragmentModuleCreationFailed";
    case PrecompiledFallbackReason::PipelineCreationFailed:
      return "PipelineCreationFailed";
    case PrecompiledFallbackReason::Unspecified:
      return "Unspecified";
    case PrecompiledFallbackReason::Count:
      return "Count";
  }
  return "Unknown";
}

const char* PrecompiledAOTStageName(PrecompiledAOTStage stage) {
  switch (stage) {
    case PrecompiledAOTStage::Attempt:
      return "attempts";
    case PrecompiledAOTStage::CacheAvailable:
      return "cacheAvailable";
    case PrecompiledAOTStage::PermutationMatched:
      return "permutationMatched";
    case PrecompiledAOTStage::ArtifactsFound:
      return "artifactsFound";
    case PrecompiledAOTStage::VertexModuleCreated:
      return "vertexModuleCreated";
    case PrecompiledAOTStage::FragmentModuleCreated:
      return "fragmentModuleCreated";
    case PrecompiledAOTStage::PipelineCreated:
      return "pipelineCreated";
    case PrecompiledAOTStage::Count:
      return "count";
  }
  return "unknown";
}

void PrecompiledShaderCache::recordArtifactHit() {
  _hitCount.fetch_add(1, std::memory_order_relaxed);
  recordAOTStage(PrecompiledAOTStage::ArtifactsFound);
}

void PrecompiledShaderCache::recordArtifactMiss(PrecompiledFallbackReason reason,
                                                const PrecompiledFallbackRecord& record) {
  _missCount.fetch_add(1, std::memory_order_relaxed);
  recordFailure(reason, record);
}

void PrecompiledShaderCache::recordFailure(PrecompiledFallbackReason reason,
                                           const PrecompiledFallbackRecord& record) {
  auto index = static_cast<size_t>(reason);
  if (index < fallbackCounts.size()) {
    fallbackCounts[index].fetch_add(1, std::memory_order_relaxed);
  }
  if (!diagnosticRecordingEnabled()) {
    return;
  }
  auto savedRecord = record;
  savedRecord.reason = reason;
  std::lock_guard<std::mutex> autoLock(diagnosticsMutex);
  _fallbackRecords.push_back(std::move(savedRecord));
}

void PrecompiledShaderCache::recordAOTStage(PrecompiledAOTStage stage,
                                            const PrecompiledHitRecord& record) {
  auto index = static_cast<size_t>(stage);
  if (index >= aotStageCounts.size()) {
    return;
  }
  aotStageCounts[index].fetch_add(1, std::memory_order_relaxed);
  if (stage != PrecompiledAOTStage::PipelineCreated || !diagnosticRecordingEnabled()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(diagnosticsMutex);
  _hitRecords.push_back(record);
}

std::vector<PrecompiledHitRecord> PrecompiledShaderCache::hitRecords() const {
  std::lock_guard<std::mutex> autoLock(diagnosticsMutex);
  return _hitRecords;
}

std::vector<PrecompiledFallbackRecord> PrecompiledShaderCache::fallbackRecords() const {
  std::lock_guard<std::mutex> autoLock(diagnosticsMutex);
  return _fallbackRecords;
}

void PrecompiledShaderCache::recordDraw(const AOTDrawStats& delta, bool complete) {
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  _drawStats.draws++;
  if (complete) {
    _drawStats.completeAOTDraws++;
  }
  _drawStats.kernelInvocations += delta.kernelInvocations;
  _drawStats.offscreenTargets += delta.offscreenTargets;
  _drawStats.materializedEdges += delta.materializedEdges;
  _drawStats.renderTargetSwitches += delta.renderTargetSwitches;
  _drawStats.intermediateReadBytes += delta.intermediateReadBytes;
  _drawStats.intermediateWriteBytes += delta.intermediateWriteBytes;
  _drawStats.peakTemporaryBytes = std::max(_drawStats.peakTemporaryBytes, delta.peakTemporaryBytes);
}

AOTDrawStats PrecompiledShaderCache::drawStats() const {
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  return _drawStats;
}

void PrecompiledShaderCache::resetStats() {
  _hitCount.store(0, std::memory_order_relaxed);
  _missCount.store(0, std::memory_order_relaxed);
  for (auto& count : aotStageCounts) {
    count.store(0, std::memory_order_relaxed);
  }
  for (auto& count : fallbackCounts) {
    count.store(0, std::memory_order_relaxed);
  }
  {
    std::lock_guard<std::mutex> drawLock(drawStatsMutex);
    _drawStats = {};
  }
  std::lock_guard<std::mutex> autoLock(diagnosticsMutex);
  _hitRecords.clear();
  _fallbackRecords.clear();
}

void PrecompiledShaderCache::unload() {
  vertEntries.clear();
  fragEntries.clear();
  _profileTag.clear();
}

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

bool PrecompiledShaderCache::loadBundle(const uint8_t* data, size_t size) {
  if (data == nullptr || size < HEADER_SIZE_V3) {
    LOGE("PrecompiledShaderCache: Bundle data too small (%zu bytes)", size);
    return false;
  }

  const uint8_t* ptr = data;
  uint32_t magic = ReadU32LE(ptr);
  if (magic != 0x54475346) {  // "TGSF"
    LOGE("PrecompiledShaderCache: Invalid magic in bundle data");
    return false;
  }
  uint16_t formatVersion = ReadU16LE(ptr + 4);
  if (formatVersion != 3) {
    LOGE("PrecompiledShaderCache: Unsupported format version %u (expected 3)", formatVersion);
    return false;
  }
  uint16_t compressionType = ReadU16LE(ptr + 6);
  if (compressionType != 0 && compressionType != 1) {
    LOGE("PrecompiledShaderCache: Unsupported compression type %u", compressionType);
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

  // Parse profileTag (32 bytes at offset 48)
  const char* tagPtr = reinterpret_cast<const char*>(ptr + 48);
  std::string profileTag(tagPtr, strnlen(tagPtr, 32));

  const uint8_t* loadPtr = ptr;
  size_t loadSize = size;
  std::vector<uint8_t> decompressed;

  if (compressionType == 1) {
    // Only the data pool region is compressed. Compute compressed size from file layout.
    size_t compressedEnd = reflectionOffset > 0 ? static_cast<size_t>(reflectionOffset) : size;
    if (dataOffset > size || compressedEnd > size || compressedEnd < dataOffset) {
      LOGE("PrecompiledShaderCache: Compressed data region out of bounds");
      return false;
    }
    size_t compressedSize = compressedEnd - static_cast<size_t>(dataOffset);
    size_t reflectionSize = reflectionOffset > 0 ? size - compressedEnd : 0;
    if (dataSize > std::numeric_limits<size_t>::max() - dataOffset ||
        reflectionSize > std::numeric_limits<size_t>::max() - dataOffset - dataSize) {
      LOGE("PrecompiledShaderCache: Decompressed bundle size overflow");
      return false;
    }
    // Decompress into a reassembled buffer: [header+pools | decompressed data | reflection]
    decompressed.resize(dataOffset + dataSize + reflectionSize);
    std::memcpy(decompressed.data(), ptr, dataOffset);
    uLongf destLen = static_cast<uLongf>(dataSize);
    int ret =
        uncompress(decompressed.data() + dataOffset, &destLen, ptr + dataOffset, compressedSize);
    if (ret != Z_OK || destLen != static_cast<uLongf>(dataSize)) {
      LOGE("PrecompiledShaderCache: zlib decompression failed (ret=%d)", ret);
      return false;
    }
    // Copy reflection section after the decompressed data.
    if (reflectionOffset > 0 && compressedEnd < size) {
      size_t newReflOffset = dataOffset + dataSize;
      std::memcpy(decompressed.data() + newReflOffset, ptr + compressedEnd, size - compressedEnd);
      reflectionOffset = static_cast<uint32_t>(newReflOffset);
    }
    loadPtr = decompressed.data();
    loadSize = decompressed.size();
  }

  std::unordered_map<HashKey, ShaderStageBlob, HashKeyHasher> newVertEntries;
  std::unordered_map<HashKey, ShaderStageBlob, HashKeyHasher> newFragEntries;
  if (!LoadPool(loadPtr, loadSize, vertPoolOffset, vertPoolCount, dataOffset, reflectionOffset,
                newVertEntries)) {
    return false;
  }
  if (!LoadPool(loadPtr, loadSize, fragPoolOffset, fragPoolCount, dataOffset, reflectionOffset,
                newFragEntries)) {
    return false;
  }

  _profileTag = std::move(profileTag);
  vertEntries = std::move(newVertEntries);
  fragEntries = std::move(newFragEntries);
  LOGI("PrecompiledShaderCache: Loaded %u vert + %u frag entries (format v%u, profile=%s)",
       vertPoolCount, fragPoolCount, formatVersion, _profileTag.c_str());
  return true;
}

bool PrecompiledShaderCache::loadBundle(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }
  auto fileSize = static_cast<size_t>(file.tellg());
  file.seekg(0);
  std::vector<uint8_t> data(fileSize);
  file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
  file.close();
  return loadBundle(data.data(), fileSize);
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
