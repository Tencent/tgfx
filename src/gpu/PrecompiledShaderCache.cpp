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
  if (statsRecordingPaused()) {
    return;
  }
  _hitCount.fetch_add(1, std::memory_order_relaxed);
  recordAOTStage(PrecompiledAOTStage::ArtifactsFound);
}

void PrecompiledShaderCache::recordArtifactMiss(PrecompiledFallbackReason reason,
                                                const PrecompiledFallbackRecord& record) {
  if (statsRecordingPaused()) {
    return;
  }
  _missCount.fetch_add(1, std::memory_order_relaxed);
  recordFailure(reason, record);
}

void PrecompiledShaderCache::recordFailure(PrecompiledFallbackReason reason,
                                           const PrecompiledFallbackRecord& record) {
  if (statsRecordingPaused()) {
    return;
  }
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
  if (statsRecordingPaused()) {
    return;
  }
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

void PrecompiledShaderCache::recordJITProgram(const JITProgramRecord& record) {
  if (statsRecordingPaused() || !diagnosticRecordingEnabled()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(diagnosticsMutex);
  _jitProgramRecords.push_back(record);
}

std::vector<JITProgramRecord> PrecompiledShaderCache::jitProgramRecords() const {
  std::lock_guard<std::mutex> autoLock(diagnosticsMutex);
  return _jitProgramRecords;
}

const char* OffscreenFillSourceName(OffscreenFillSource source) {
  switch (source) {
    case OffscreenFillSource::Unknown:
      return "Unknown";
    case OffscreenFillSource::FPFlatten:
      return "FPFlatten";
    case OffscreenFillSource::GaussianBlur:
      return "GaussianBlur";
    case OffscreenFillSource::ImageFilter:
      return "ImageFilter";
    case OffscreenFillSource::TransformImage:
      return "TransformImage";
    case OffscreenFillSource::RGBAAAImage:
      return "RGBAAAImage";
    case OffscreenFillSource::Count:
      break;
  }
  return "Unknown";
}

void PrecompiledShaderCache::recordDraw(const AOTDrawStats& delta, bool complete) {
  if (statsRecordingPaused()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  _drawStats.draws++;
  if (complete) {
    _drawStats.completeAOTDraws++;
  }
  _drawStats.atomicFallbacks += delta.atomicFallbacks;
  _drawStats.kernelInvocations += delta.kernelInvocations;
  _drawStats.offscreenTargets += delta.offscreenTargets;
  _drawStats.materializedEdges += delta.materializedEdges;
  _drawStats.renderTargetSwitches += delta.renderTargetSwitches;
  _drawStats.intermediateReadBytes += delta.intermediateReadBytes;
  _drawStats.intermediateWriteBytes += delta.intermediateWriteBytes;
  _drawStats.peakTemporaryBytes = std::max(_drawStats.peakTemporaryBytes, delta.peakTemporaryBytes);
}

void PrecompiledShaderCache::recordMaterializedEdge(uint64_t bytes) {
  if (statsRecordingPaused()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  _drawStats.materializedEdges++;
  _drawStats.offscreenTargets++;
  _drawStats.renderTargetSwitches++;
  _drawStats.intermediateReadBytes += bytes;
  _drawStats.intermediateWriteBytes += bytes;
}

AOTDrawStats PrecompiledShaderCache::drawStats() const {
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  return _drawStats;
}

static const char* OffscreenKernelName(AOTKernelKind kernel) {
  switch (kernel) {
    case AOTKernelKind::TextureFill:
      return "TextureFill";
    case AOTKernelKind::TextureColorMatrix:
      return "TextureColorMatrix";
    case AOTKernelKind::TexturedColorMatrix:
      return "TexturedColorMatrix";
    case AOTKernelKind::TexturedLuma:
      return "TexturedLuma";
    case AOTKernelKind::PointwiseChain:
      return "PointwiseChain";
    case AOTKernelKind::PointwiseTail:
      return "PointwiseTail";
    case AOTKernelKind::PerlinNoiseFill:
      return "PerlinNoiseFill";
  }
  return "Unknown";
}

OffscreenFillKey PrecompiledShaderCache::recordOffscreenFillAnalysis(
    OffscreenFillSource source, bool coordOffsetNonZero, const std::string& topLevelProcessor,
    bool lowerSucceeded, const std::string& lowerBlocker, bool validateSucceeded,
    bool decomposeSucceeded, bool canExecute, const std::vector<AOTKernelKind>& kernels) {
  if (statsRecordingPaused() || !diagnosticRecordingEnabled()) {
    return InvalidOffscreenFillKey;
  }
  std::string kernelSignature = "None";
  if (!kernels.empty()) {
    kernelSignature.clear();
    for (size_t i = 0; i < kernels.size(); ++i) {
      if (i != 0) {
        kernelSignature += ">";
      }
      kernelSignature += OffscreenKernelName(kernels[i]);
    }
  }
  auto key = _nextOffscreenFillKey.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> autoLock(offscreenFillStatsMutex);
  auto& stats = _offscreenFillStats[static_cast<size_t>(source)];
  stats.calls++;
  if (coordOffsetNonZero) {
    stats.coordOffsetNonZero++;
  }
  stats.topLevelProcessors[topLevelProcessor]++;
  if (!lowerSucceeded) {
    stats.lowerBlockers[lowerBlocker.empty() ? "Unknown" : lowerBlocker]++;
  } else {
    stats.lowerSucceeded++;
    if (validateSucceeded) {
      stats.validateSucceeded++;
      if (decomposeSucceeded) {
        stats.decomposeSucceeded++;
        if (kernels.size() > 1) {
          stats.multiPassPlans++;
        } else if (kernels.size() == 1 && kernels[0] == AOTKernelKind::PointwiseChain) {
          stats.pointwiseChainPlans++;
        } else if (kernels.size() == 1 && kernels[0] == AOTKernelKind::PointwiseTail) {
          stats.pointwiseTailPlans++;
        }
        if (canExecute) {
          stats.canExecute++;
        }
      }
    }
  }
  std::string aggregateKey = std::string(OffscreenFillSourceName(source)) + "|" +
                             topLevelProcessor + "|" + kernelSignature;
  auto& aggregate = _offscreenFillCorrelations[aggregateKey];
  aggregate.source = source;
  aggregate.topLevelProcessor = topLevelProcessor;
  aggregate.kernelSignature = kernelSignature;
  aggregate.calls++;
  if (canExecute) {
    aggregate.canExecute++;
  }
  _offscreenFillRecords.emplace(
      key, OffscreenFillStaticRecord{source, topLevelProcessor, kernelSignature, canExecute});
  return key;
}

void PrecompiledShaderCache::recordOffscreenFillProgram(OffscreenFillKey key,
                                                        ProgramOrigin origin) {
  if (statsRecordingPaused() || !diagnosticRecordingEnabled() || key == InvalidOffscreenFillKey) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(offscreenFillStatsMutex);
  auto record = _offscreenFillRecords.find(key);
  if (record == _offscreenFillRecords.end()) {
    return;
  }
  auto& stats = _offscreenFillStats[static_cast<size_t>(record->second.source)];
  std::string aggregateKey = std::string(OffscreenFillSourceName(record->second.source)) + "|" +
                             record->second.topLevelProcessor + "|" +
                             record->second.kernelSignature;
  auto& aggregate = _offscreenFillCorrelations[aggregateKey];
  if (origin == ProgramOrigin::PrecompiledArtifact) {
    stats.precompiledPrograms++;
    aggregate.precompiledPrograms++;
    if (record->second.canExecute) {
      stats.precompiledCanExecute++;
      aggregate.precompiledCanExecute++;
    }
  } else {
    stats.programBuilderPrograms++;
    aggregate.programBuilderPrograms++;
    if (record->second.canExecute) {
      stats.programBuilderCanExecute++;
      aggregate.programBuilderCanExecute++;
    }
  }
  _offscreenFillRecords.erase(record);
}

OffscreenFillStats PrecompiledShaderCache::offscreenFillStats() const {
  std::lock_guard<std::mutex> autoLock(offscreenFillStatsMutex);
  OffscreenFillStats total = {};
  for (const auto& stats : _offscreenFillStats) {
    total.calls += stats.calls;
    total.coordOffsetNonZero += stats.coordOffsetNonZero;
    total.lowerSucceeded += stats.lowerSucceeded;
    total.validateSucceeded += stats.validateSucceeded;
    total.decomposeSucceeded += stats.decomposeSucceeded;
    total.canExecute += stats.canExecute;
    total.pointwiseChainPlans += stats.pointwiseChainPlans;
    total.pointwiseTailPlans += stats.pointwiseTailPlans;
    total.multiPassPlans += stats.multiPassPlans;
    total.precompiledPrograms += stats.precompiledPrograms;
    total.programBuilderPrograms += stats.programBuilderPrograms;
    total.precompiledCanExecute += stats.precompiledCanExecute;
    total.programBuilderCanExecute += stats.programBuilderCanExecute;
    for (const auto& [name, count] : stats.lowerBlockers) {
      total.lowerBlockers[name] += count;
    }
    for (const auto& [name, count] : stats.topLevelProcessors) {
      total.topLevelProcessors[name] += count;
    }
  }
  return total;
}

std::vector<OffscreenFillCorrelation> PrecompiledShaderCache::offscreenFillCorrelations() const {
  std::lock_guard<std::mutex> autoLock(offscreenFillStatsMutex);
  std::vector<OffscreenFillCorrelation> result = {};
  result.reserve(_offscreenFillCorrelations.size());
  for (const auto& [key, correlation] : _offscreenFillCorrelations) {
    result.push_back(correlation);
  }
  return result;
}

std::array<OffscreenFillStats, static_cast<size_t>(OffscreenFillSource::Count)>
PrecompiledShaderCache::offscreenFillStatsBySource() const {
  std::lock_guard<std::mutex> autoLock(offscreenFillStatsMutex);
  return _offscreenFillStats;
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
  {
    std::lock_guard<std::mutex> offscreenLock(offscreenFillStatsMutex);
    _nextOffscreenFillKey.store(1, std::memory_order_relaxed);
    _offscreenFillStats = {};
    _offscreenFillRecords.clear();
    _offscreenFillCorrelations.clear();
  }
  std::lock_guard<std::mutex> autoLock(diagnosticsMutex);
  _hitRecords.clear();
  _fallbackRecords.clear();
  _jitProgramRecords.clear();
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
                               std::vector<Uniform>& out, bool hasArraySize) {
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
    uint32_t arraySize = 1;
    if (hasArraySize) {
      if (*offset + 2 > maxLen) {
        return false;
      }
      arraySize =
          static_cast<uint32_t>(data[*offset]) | (static_cast<uint32_t>(data[*offset + 1]) << 8);
      *offset += 2;
    }
    out.emplace_back(std::move(name), format, arraySize);
  }
  return true;
}

// Reflection format: [uniformCount:u8][samplerCount:u8][reserved:u8][reserved:u8]
//                    For each uniform: [nameLen:u8][name:bytes][format:u8]
//                                      (+[arraySize:u16] since bundle v4)
//                    For each sampler: [nameLen:u8][name:bytes][format:u8]
//                                      (+[arraySize:u16] since bundle v4)
static bool ParseStageReflection(const uint8_t* data, size_t maxLen, ShaderStageBlob* blob,
                                 bool hasArraySize) {
  if (maxLen < 4) {
    return false;
  }
  size_t offset = 0;
  uint8_t uniformCount = data[offset++];
  uint8_t samplerCount = data[offset++];
  offset += 2;  // reserved

  if (!ReadUniformEntries(data, maxLen, &offset, uniformCount, blob->uniforms, hasArraySize)) {
    return false;
  }
  if (!ReadUniformEntries(data, maxLen, &offset, samplerCount, blob->samplers, hasArraySize)) {
    return false;
  }
  return true;
}

static bool LoadPool(const uint8_t* fileData, size_t fileSize, uint32_t poolOffset,
                     uint32_t poolCount, uint32_t dataOffset, uint32_t reflectionOffset,
                     std::unordered_map<PrecompiledShaderCache::HashKey, ShaderStageBlob,
                                        PrecompiledShaderCache::HashKeyHasher>& entries,
                     bool hasArraySize) {
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
        if (!ParseStageReflection(fileData + absReflOff, maxReflLen, &blob, hasArraySize)) {
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
  // v4 adds an arraySize field to every reflection entry; v3 entries parse as scalars.
  if (formatVersion != 3 && formatVersion != 4) {
    LOGE("PrecompiledShaderCache: Unsupported format version %u (expected 3 or 4)", formatVersion);
    return false;
  }
  const bool hasArraySize = formatVersion >= 4;
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
                newVertEntries, hasArraySize)) {
    return false;
  }
  if (!LoadPool(loadPtr, loadSize, fragPoolOffset, fragPoolCount, dataOffset, reflectionOffset,
                newFragEntries, hasArraySize)) {
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
