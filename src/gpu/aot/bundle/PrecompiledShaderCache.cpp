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
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <tuple>
#include "core/utils/Log.h"
#include "gpu/GlobalCache.h"
#include "gpu/ShaderCaps.h"
#include "gpu/aot/select/PermutationMatch.h"
#include "gpu/shaders/BundleCodec.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gpu/shaders/contract/ShaderProfileResolver.h"
#include "tgfx/gpu/Context.h"
#include "zstd.h"

namespace tgfx {

PrecompiledShaderCache::PrecompiledShaderCache(Context* context, Backend backend)
    : _context(context), _backend(backend), _shaderCache(context) {
}

const char* PrecompiledFallbackReasonName(PrecompiledFallbackReason reason) {
  return AOTFailureName(reason);
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
  if (statsRecordingPaused() || missPaused.load(std::memory_order_relaxed)) {
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
  savedRecord.deliberate = deliberateMarking.load(std::memory_order_relaxed);
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
  _drawStats.directChainDraws += delta.directChainDraws;
  _drawStats.offscreenPlanDraws += delta.offscreenPlanDraws;
  _drawStats.fpFlattenEdges += delta.fpFlattenEdges;
  _drawStats.planMaterializedEdges += delta.planMaterializedEdges;
  _drawStats.planExecutionFailures += delta.planExecutionFailures;
  for (size_t index = 0; index < _drawStats.planPassHistogram.size(); ++index) {
    _drawStats.planPassHistogram[index] += delta.planPassHistogram[index];
  }
}

void PrecompiledShaderCache::recordDecomposeRejection(AOTDecomposeOutcome outcome) {
  if (statsRecordingPaused()) {
    return;
  }
  auto index = static_cast<size_t>(outcome);
  if (index >= _drawStats.decomposeRejections.size()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  _drawStats.decomposeRejections[index]++;
}

void PrecompiledShaderCache::recordDroppedDraw() {
  if (statsRecordingPaused()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  if (deliberateMissMarking()) {
    _drawStats.deliberateDroppedDraws++;
  } else {
    _drawStats.droppedDraws++;
  }
}

void PrecompiledShaderCache::settleActiveTemporaryBytes() {
  if (statsRecordingPaused()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  _drawStats.peakTemporaryBytes = std::max(_drawStats.peakTemporaryBytes, _activeTemporaryBytes);
  _activeTemporaryBytes = 0;
}

void PrecompiledShaderCache::recordActiveTemporaryBytes(uint64_t bytes) {
  if (statsRecordingPaused()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  _activeTemporaryBytes += bytes;
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

// The header and pool entry layout is defined in ShaderBundleContract.h, shared with the bundle writer.
// Independent bounds for the input/reassembled file and for copied shader/reflection payloads.
// These are parser limits, not a bound on total process RSS or concurrently retained old bundles.
static constexpr size_t MAX_BUNDLE_BYTES = 256u * 1024u * 1024u;
static constexpr size_t MAX_DECODED_PAYLOAD_BYTES = 256u * 1024u * 1024u;
static constexpr uint32_t MAX_BUNDLE_ENTRIES = 65536;

static bool ConsumePayloadBudget(size_t bytes, size_t* remainingBytes) {
  if (bytes > *remainingBytes) {
    return false;
  }
  *remainingBytes -= bytes;
  return true;
}

static bool IsValidReflectionFormat(UniformFormat format, bool sampler) {
  switch (format) {
    case UniformFormat::Float:
    case UniformFormat::Float2:
    case UniformFormat::Float3:
    case UniformFormat::Float4:
    case UniformFormat::Float2x2:
    case UniformFormat::Float3x3:
    case UniformFormat::Float4x4:
    case UniformFormat::Int:
    case UniformFormat::Int2:
    case UniformFormat::Int3:
    case UniformFormat::Int4:
      return !sampler;
    case UniformFormat::Texture2DSampler:
    case UniformFormat::TextureExternalSampler:
    case UniformFormat::Texture2DRectSampler:
      return sampler;
    default:
      return false;
  }
}

static bool ParseStageReflection(const uint8_t* data, size_t maxLen, ShaderStageBlob* blob,
                                 size_t* remainingBytes) {
  // The byte grammar is single-sourced in BundleCodec (the tool's writer goes through it too);
  // this side keeps the semantic rules the enum owns: which formats may be uniforms or samplers,
  // sampler array sizes, and vertex-input locations that strictly increase.
  BundleReflection reflection = {};
  if (!ParseBundleReflection(data, maxLen, &reflection)) {
    return false;
  }
  size_t budgetBytes = reflection.uniforms.size() * sizeof(Uniform) +
                       reflection.samplers.size() * sizeof(Uniform) +
                       reflection.vertexInputs.size() * sizeof(VertexInput);
  for (const auto* list : {&reflection.uniforms, &reflection.samplers}) {
    for (const auto& entry : *list) {
      budgetBytes += entry.name.size() + 1;
    }
  }
  for (const auto& entry : reflection.vertexInputs) {
    budgetBytes += entry.name.size() + 2;
  }
  if (!ConsumePayloadBudget(budgetBytes, remainingBytes)) {
    return false;
  }
  blob->uniforms.reserve(reflection.uniforms.size());
  for (auto& entry : reflection.uniforms) {
    auto format = static_cast<UniformFormat>(entry.format);
    if (!IsValidReflectionFormat(format, false) || entry.arraySize == 0) {
      return false;
    }
    blob->uniforms.emplace_back(std::move(entry.name), format,
                                static_cast<uint32_t>(entry.arraySize));
  }
  blob->samplers.reserve(reflection.samplers.size());
  for (auto& entry : reflection.samplers) {
    auto format = static_cast<UniformFormat>(entry.format);
    if (!IsValidReflectionFormat(format, true) || entry.arraySize != 1) {
      return false;
    }
    blob->samplers.emplace_back(std::move(entry.name), format, 1u);
  }
  bool hasPreviousLocation = false;
  uint32_t previousLocation = 0;
  blob->vertexInputs.reserve(reflection.vertexInputs.size());
  for (auto& entry : reflection.vertexInputs) {
    auto format = static_cast<UniformFormat>(entry.format);
    if (!IsValidReflectionFormat(format, false)) {
      LOGE("PrecompiledShaderCache: vertex input '%s' has invalid format %u", entry.name.c_str(),
           static_cast<uint32_t>(entry.format));
      return false;
    }
    auto location = static_cast<uint32_t>(entry.location);
    // The entries are stored in location order, so a decreasing or duplicated location means the
    // .vert's own layout declarations (or the serialized order) are inconsistent — the backends
    // bind attribute slots by location, and a duplicate would silently leave one slot unbound.
    // A gap between locations is legal: the GP's optional slots leave holes on purpose
    // (shape-instanced kernels skip location 1), so contiguity is not required.
    if (hasPreviousLocation && location <= previousLocation) {
      LOGE(
          "PrecompiledShaderCache: vertex input '%s' location %u does not increase (previous "
          "%u)",
          entry.name.c_str(), location, previousLocation);
      return false;
    }
    previousLocation = location;
    hasPreviousLocation = true;
    blob->vertexInputs.push_back(VertexInput{std::move(entry.name), format, location});
  }
  return true;
}

// Pool table spans have already been checked. Account for aliases separately: every entry
// becomes an owned blob, even when several keys refer to the same serialized byte range.
static bool CheckPoolPayloadBudget(const uint8_t* fileData, size_t poolOffset, uint32_t poolCount,
                                   size_t dataPoolSize, size_t* remainingBytes) {
  for (uint32_t index = 0; index < poolCount; ++index) {
    auto entry = fileData + poolOffset + static_cast<size_t>(index) * BundleFormat::PoolEntrySize;
    auto blobOffset = ReadU32LE(entry + BundleFormat::EntryOffset::DataOffset);
    auto blobSize = ReadU32LE(entry + BundleFormat::EntryOffset::DataSize);
    if (blobOffset > dataPoolSize || blobSize > dataPoolSize - blobOffset ||
        !ConsumePayloadBudget(sizeof(ShaderStageBlob) + sizeof(PrecompiledShaderCache::HashKey),
                              remainingBytes) ||
        !ConsumePayloadBudget(blobSize, remainingBytes)) {
      return false;
    }
  }
  return true;
}

static bool LoadPool(const uint8_t* fileData, size_t fileSize, size_t poolOffset,
                     uint32_t poolCount, size_t dataPoolStart, size_t dataPoolEnd,
                     size_t reflectionPoolStart,
                     std::unordered_map<PrecompiledShaderCache::HashKey, ShaderStageBlob,
                                        PrecompiledShaderCache::HashKeyHasher>& entries,
                     size_t* remainingBytes) {
  for (uint32_t i = 0; i < poolCount; i++) {
    size_t entryOff = poolOffset + static_cast<size_t>(i) * BundleFormat::PoolEntrySize;
    if (entryOff > fileSize || BundleFormat::PoolEntrySize > fileSize - entryOff) {
      LOGE("PrecompiledShaderCache: Pool entry %u out of bounds", i);
      return false;
    }
    const uint8_t* entry = fileData + entryOff;
    uint64_t hashHi = ReadU64LE(entry + BundleFormat::EntryOffset::HashHi);
    uint64_t hashLo = ReadU64LE(entry + BundleFormat::EntryOffset::HashLo);
    uint32_t blobOff = ReadU32LE(entry + BundleFormat::EntryOffset::DataOffset);
    uint32_t blobSize = ReadU32LE(entry + BundleFormat::EntryOffset::DataSize);
    uint32_t reflOff = ReadU32LE(entry + BundleFormat::EntryOffset::ReflectionOffset);

    auto dataPoolSize = dataPoolEnd - dataPoolStart;
    if (blobOff > dataPoolSize || blobSize > dataPoolSize - blobOff) {
      LOGE("PrecompiledShaderCache: Data blob outside the data pool for entry %u", i);
      return false;
    }
    size_t absDataOff = dataPoolStart + blobOff;

    ShaderStageBlob blob;
    blob.data.assign(fileData + absDataOff, fileData + absDataOff + blobSize);

    if (reflectionPoolStart != 0) {
      if (reflectionPoolStart > fileSize || reflOff >= fileSize - reflectionPoolStart) {
        LOGE("PrecompiledShaderCache: Reflection out of bounds for entry %u", i);
        return false;
      }
      size_t absReflOff = reflectionPoolStart + reflOff;
      if (!ParseStageReflection(fileData + absReflOff, fileSize - absReflOff, &blob,
                                remainingBytes)) {
        LOGE("PrecompiledShaderCache: Failed to parse reflection for entry %u", i);
        return false;
      }
    }

    PrecompiledShaderCache::HashKey key{hashHi, hashLo};
    auto existing = entries.find(key);
    if (existing != entries.end()) {
      // A repeated stage key is only legal as byte-identical duplication; the writer rejects
      // conflicting content at build time, so a real conflict here means the bundle was hand-
      // edited or corrupted — reject it instead of silently keeping the last one.
      auto uniformListsEqual = [](const std::vector<Uniform>& a, const std::vector<Uniform>& b) {
        if (a.size() != b.size()) {
          return false;
        }
        for (size_t u = 0; u < a.size(); ++u) {
          if (a[u].name() != b[u].name() || a[u].format() != b[u].format() ||
              a[u].arraySize() != b[u].arraySize()) {
            return false;
          }
        }
        return true;
      };
      auto vertexInputsEqual = [](const std::vector<VertexInput>& a,
                                  const std::vector<VertexInput>& b) {
        if (a.size() != b.size()) {
          return false;
        }
        for (size_t u = 0; u < a.size(); ++u) {
          if (a[u].name != b[u].name || a[u].format != b[u].format ||
              a[u].location != b[u].location) {
            return false;
          }
        }
        return true;
      };
      if (existing->second.data != blob.data ||
          !uniformListsEqual(existing->second.uniforms, blob.uniforms) ||
          !uniformListsEqual(existing->second.samplers, blob.samplers) ||
          !vertexInputsEqual(existing->second.vertexInputs, blob.vertexInputs)) {
        LOGE(
            "PrecompiledShaderCache: Conflicting content for duplicate pool entry (key "
            "%016llx%016llx)",
            static_cast<unsigned long long>(hashHi), static_cast<unsigned long long>(hashLo));
        return false;
      }
    } else {
      entries[key] = std::move(blob);
    }
  }
  return true;
}

// Feeds the pool entries into the bundle identity hash in the on-disk field order, the sequence the
// writer feeds (see BundleIdentityHashEntry).
static uint64_t HashPoolEntries(uint64_t state, const uint8_t* pool, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* entry = pool + static_cast<size_t>(i) * BundleFormat::PoolEntrySize;
    state = BundleIdentityHashEntry(state, ReadU64LE(entry + BundleFormat::EntryOffset::HashHi),
                                    ReadU64LE(entry + BundleFormat::EntryOffset::HashLo),
                                    ReadU32LE(entry + BundleFormat::EntryOffset::DataOffset),
                                    ReadU32LE(entry + BundleFormat::EntryOffset::DataSize),
                                    ReadU32LE(entry + BundleFormat::EntryOffset::ReflectionOffset));
  }
  return state;
}

std::unordered_map<std::pair<uint64_t, size_t>,
                   std::weak_ptr<const PrecompiledShaderCache::ParsedBundle>,
                   PrecompiledShaderCache::SharedBundleKeyHasher>&
PrecompiledShaderCache::SharedBundleRegistry() {
  static std::unordered_map<std::pair<uint64_t, size_t>, std::weak_ptr<const ParsedBundle>,
                            SharedBundleKeyHasher>
      registry;
  return registry;
}

static std::mutex& SharedBundleRegistryMutex() {
  static std::mutex mutex;
  return mutex;
}

std::shared_ptr<const PrecompiledShaderCache::ParsedBundle>
PrecompiledShaderCache::FindSharedBundle(uint64_t identityHash, size_t size) {
  std::lock_guard<std::mutex> lock(SharedBundleRegistryMutex());
  auto it = SharedBundleRegistry().find({identityHash, size});
  return it == SharedBundleRegistry().end() ? nullptr : it->second.lock();
}

std::shared_ptr<const PrecompiledShaderCache::ParsedBundle>
PrecompiledShaderCache::ShareParsedBundle(uint64_t identityHash, size_t size, ParsedBundle parsed) {
  std::lock_guard<std::mutex> lock(SharedBundleRegistryMutex());
  auto& registry = SharedBundleRegistry();
  auto it = registry.find({identityHash, size});
  if (it != registry.end()) {
    if (auto existing = it->second.lock()) {
      return existing;
    }
  }
  auto shared = std::make_shared<const ParsedBundle>(std::move(parsed));
  registry[{identityHash, size}] = shared;
  return shared;
}

bool PrecompiledShaderCache::loadBundle(const uint8_t* data, size_t size) {
  if (data == nullptr || size < BundleFormat::HeaderSize || size > MAX_BUNDLE_BYTES) {
    LOGE("PrecompiledShaderCache: Bundle size outside parser limits (%zu bytes)", size);
    return false;
  }

  const uint8_t* ptr = data;
  // The header grammar is single-sourced in BundleCodec (the tool's verifier and manifest go
  // through the same parser); the checks below are this loader's policy.
  BundleHeader header = {};
  if (!ParseBundleHeader(ptr, size, &header)) {
    LOGE("PrecompiledShaderCache: Bundle header is shorter than the format requires");
    return false;
  }
  if (header.magic != BundleFormat::Magic) {
    LOGE("PrecompiledShaderCache: Invalid magic in bundle data");
    return false;
  }
  if (header.formatVersion != BundleFormat::CurrentVersion) {
    LOGE("PrecompiledShaderCache: Unsupported format version %u (expected %u)",
         header.formatVersion, BundleFormat::CurrentVersion);
    return false;
  }
  // The format defines exactly two stored forms (None and Zstd). A value in between (1) names no
  // dialect the loader can interpret: reading it as uncompressed would feed a zstd-compressed
  // data pool to the parser as raw bytes, so it is refused like every other unknown value.
  if (header.compressionType != BundleFormat::Compression::None &&
      header.compressionType != BundleFormat::Compression::Zstd) {
    LOGE("PrecompiledShaderCache: Unsupported compression type %u", header.compressionType);
    return false;
  }
  // The payload kind decides which ShaderCodeFormat the blobs are handed to the backend as. An
  // unknown value names no form the loader could pick, so it is refused like the other enum fields.
  if (header.payloadFormat > BundleFormat::Payload::Max) {
    LOGE("PrecompiledShaderCache: Unsupported payload format %u", header.payloadFormat);
    return false;
  }
  uint64_t sourceHash = header.identityHash;
  // Toolchain ABI gate (F04): the content identity hash covers the bytes, but not what they
  // mean. The recorded toolchain version is the one contract every writer has stamped since the
  // field existed, so a mismatch here means the bundle's reflection grammar or uniform contracts
  // predate (or postdate) what this runtime knows how to consume — reject instead of guessing.
  if (header.toolchainABI != ExpectedToolchainABI) {
    LOGE(
        "PrecompiledShaderCache: Bundle toolchain ABI 0x%08x does not match the runtime's "
        "0x%08x; the reflection/uniform contracts are incompatible",
        header.toolchainABI, ExpectedToolchainABI);
    return false;
  }
  uint32_t vertPoolCount = header.vertPoolCount;
  uint32_t fragPoolCount = header.fragPoolCount;
  uint32_t vertPoolOffset = header.vertPoolOffset;
  uint32_t fragPoolOffset = header.fragPoolOffset;
  uint32_t dataOffset = header.dataOffset;
  uint32_t dataSize = header.dataSize;
  uint32_t reflectionOffset = header.reflectionOffset;
  const std::string& profileTag = header.profileTag;
  // The parsed fields the rest of this loader and the cached ParsedBundle carry forward.
  const uint16_t formatVersion = header.formatVersion;
  const uint16_t compressionType = header.compressionType;
  const uint32_t toolchainVersion = header.toolchainABI;

  // Backend identity: a bundle compiled for a different backend must be rejected outright. The
  // format version says nothing about the code dialect or binary format of the blobs, so a
  // same-name cross-backend bundle would otherwise feed mismatched code to the GPU. A cache
  // constructed without a backend (standalone parsing in tests) skips this check: an unspecified
  // identity cannot be verified.
  const auto expectedProfile = ExpectedShaderProfile(_backend);
  // An OpenGL context running the ES standard (GLES devices, SwiftShader, WebGL) must be served
  // from the opengles bundle: the desktop tag only covers desktop GLSL profiles. The GL profile
  // is a property of the context rather than the backend enum, so a cache without a context
  // (standalone parsing) leaves the expected profile unspecified and accepts either GL tag,
  // mirroring the unspecified-identity skip below.
  const bool esProfileContext =
      _backend == Backend::OpenGL && _context != nullptr &&
      ResolveGLShaderProfile(_context->shaderCaps()->usesPrecisionModifiers) ==
          ShaderProfile::OpenGLES;
  const bool profileUnspecified = _backend == Backend::OpenGL && _context == nullptr;
  // Exactly one profile serves a context: accepting the other GL tag here would load a bundle
  // every draw is then refused against at match time, which is a silent full-frame loss.
  const std::optional<ShaderProfile> requiredProfile =
      profileUnspecified || !expectedProfile
          ? std::nullopt
          : std::optional<ShaderProfile>(RequiredProfile(*expectedProfile, esProfileContext));
  const bool tagAccepted = !requiredProfile || profileTag == ProfileTag(*requiredProfile);
  if (requiredProfile && !tagAccepted) {
    LOGE(
        "PrecompiledShaderCache: Bundle profile tag '%s' does not match the running backend "
        "(expected '%s')",
        profileTag.c_str(), ProfileTag(*requiredProfile));
    return false;
  }
  // The profile fixes which payload forms are legal (see ProfileAllowsPayload). A kind outside it
  // is refused at load instead of being handed to the backend as a format it cannot be: a Vulkan
  // bundle claiming text, for example, would have its SPIR-V words read through the text field.
  if (requiredProfile && !ProfileAllowsPayload(*requiredProfile, header.payloadFormat)) {
    LOGE("PrecompiledShaderCache: Payload format %u is not one the '%s' profile can store",
         header.payloadFormat, profileTag.c_str());
    return false;
  }

  const uint8_t* loadPtr = ptr;
  size_t loadSize = size;
  std::vector<uint8_t> decompressed;

  // Validate spans before any multiplication, pointer arithmetic or hash traversal. size_t is
  // only 32 bits on wasm32 and some mobile targets, so casting the header fields is not enough.
  auto dataPoolStart = static_cast<size_t>(dataOffset);
  if (vertPoolOffset < BundleFormat::HeaderSize || vertPoolOffset > fragPoolOffset ||
      fragPoolOffset > dataPoolStart || dataPoolStart > size ||
      vertPoolCount >
          (static_cast<size_t>(fragPoolOffset) - vertPoolOffset) / BundleFormat::PoolEntrySize ||
      fragPoolCount > (dataPoolStart - fragPoolOffset) / BundleFormat::PoolEntrySize) {
    LOGE("PrecompiledShaderCache: Pool sections overlap or exceed the file");
    return false;
  }
  if (dataSize > MAX_BUNDLE_BYTES - dataPoolStart || vertPoolCount > MAX_BUNDLE_ENTRIES ||
      fragPoolCount > MAX_BUNDLE_ENTRIES - vertPoolCount) {
    LOGE("PrecompiledShaderCache: Bundle data or entry count exceeds parser limits");
    return false;
  }
  auto dataPoolEnd = dataPoolStart + dataSize;
  size_t remainingPayloadBytes = MAX_DECODED_PAYLOAD_BYTES;
  if (!CheckPoolPayloadBudget(ptr, vertPoolOffset, vertPoolCount, dataSize,
                              &remainingPayloadBytes) ||
      !CheckPoolPayloadBudget(ptr, fragPoolOffset, fragPoolCount, dataSize,
                              &remainingPayloadBytes)) {
    LOGE("PrecompiledShaderCache: Invalid blob span or decoded payload budget exceeded");
    return false;
  }

  size_t reflectionPoolStart = reflectionOffset;

  if (compressionType == BundleFormat::Compression::Zstd) {
    // Only the data pool region is compressed. Compute compressed size from file layout.
    size_t compressedEnd = reflectionOffset > 0 ? static_cast<size_t>(reflectionOffset) : size;
    if (dataPoolStart > size || compressedEnd > size || compressedEnd < dataPoolStart) {
      LOGE("PrecompiledShaderCache: Compressed data region out of bounds");
      return false;
    }
    size_t compressedSize = compressedEnd - dataPoolStart;
    size_t reflectionSize = reflectionOffset > 0 ? size - compressedEnd : 0;
    if (reflectionSize > MAX_BUNDLE_BYTES - dataPoolEnd) {
      LOGE("PrecompiledShaderCache: Reassembled bundle exceeds parser limits");
      return false;
    }
    // Decompress into a reassembled buffer: [header+pools | decompressed data | reflection]
    decompressed.resize(dataPoolEnd + reflectionSize);
    std::memcpy(decompressed.data(), ptr, dataPoolStart);
    size_t destLen = ZSTD_decompress(decompressed.data() + dataPoolStart, dataSize,
                                     ptr + dataPoolStart, compressedSize);
    bool decompressOK = !ZSTD_isError(destLen) && destLen == dataSize;
    if (!decompressOK) {
      LOGE("PrecompiledShaderCache: data pool decompression failed (type=%u)", compressionType);
      return false;
    }
    // Copy reflection section after the decompressed data.
    if (reflectionSize > 0) {
      std::memcpy(decompressed.data() + dataPoolEnd, ptr + compressedEnd, reflectionSize);
      reflectionPoolStart = dataPoolEnd;
    } else {
      reflectionPoolStart = 0;
    }
    loadPtr = decompressed.data();
    loadSize = decompressed.size();
  } else if (dataPoolEnd > size || (reflectionOffset != 0 &&
                                    (dataPoolEnd > reflectionOffset || reflectionOffset > size))) {
    LOGE("PrecompiledShaderCache: Data or reflection section out of bounds");
    return false;
  }

  // Content identity: recompute the writer's identity hash over the loaded view (pool entries,
  // uncompressed data pool, reflection pool, tag, counts) and reject mismatches. The hash is
  // mandatory — bundles written before the identity contract existed are refused with every
  // other bundle older than CurrentVersion.
  if (sourceHash == 0) {
    LOGE(
        "PrecompiledShaderCache: Bundle carries no identity hash; refusing an unverifiable bundle");
    return false;
  }
  {
    uint64_t computed = BundleIdentityHashInit();
    computed = BundleIdentityHashHeader(computed, formatVersion, vertPoolCount, fragPoolCount,
                                        ptr + BundleFormat::Offset::ProfileTag);
    computed = HashPoolEntries(computed, loadPtr + vertPoolOffset, vertPoolCount);
    computed = HashPoolEntries(computed, loadPtr + fragPoolOffset, fragPoolCount);
    computed = BundleIdentityHashBytes(computed, loadPtr + dataPoolStart, dataSize);
    if (reflectionPoolStart != 0 && loadSize > reflectionPoolStart) {
      computed = BundleIdentityHashBytes(computed, loadPtr + reflectionPoolStart,
                                         loadSize - reflectionPoolStart);
    }
    if (computed != sourceHash) {
      LOGE(
          "PrecompiledShaderCache: Bundle identity hash mismatch (expected 0x%016llx, computed "
          "0x%016llx); the content does not match its recorded identity",
          static_cast<unsigned long long>(sourceHash), static_cast<unsigned long long>(computed));
      return false;
    }
  }

  // Content-addressed sharing, after verification: every Context that loads the same bundle bytes
  // adopts the one parse this process made. The key is the verified identity plus the byte size,
  // so the registry can never serve content a tampered bundle merely claims.
  if (auto shared = FindSharedBundle(sourceHash, size)) {
    _bundle = std::move(shared);
    {
      // The same reset the fresh-parse path applies below: a program miss recorded against the
      // previous generation must not keep refusing a key the newly loaded bundle can serve.
      std::lock_guard<std::mutex> lock(_programMissMutex);
      _programMisses.clear();
    }
    _generation.fetch_add(1, std::memory_order_relaxed);
    if (_context != nullptr) {
      _context->globalCache()->clearPrograms();
    }
    LOGI("PrecompiledShaderCache: Adopted the shared parse of profile=%s (%zu bytes)",
         _bundle->profileTag.c_str(), size);
    return true;
  }

  std::unordered_map<HashKey, ShaderStageBlob, HashKeyHasher> newVertEntries;
  std::unordered_map<HashKey, ShaderStageBlob, HashKeyHasher> newFragEntries;
  if (!LoadPool(loadPtr, loadSize, vertPoolOffset, vertPoolCount, dataPoolStart, dataPoolEnd,
                reflectionPoolStart, newVertEntries, &remainingPayloadBytes)) {
    return false;
  }
  if (!LoadPool(loadPtr, loadSize, fragPoolOffset, fragPoolCount, dataPoolStart, dataPoolEnd,
                reflectionPoolStart, newFragEntries, &remainingPayloadBytes)) {
    return false;
  }

  ParsedBundle parsed;
  parsed.profileTag = std::move(profileTag);
  parsed.identityHash = sourceHash;
  parsed.toolchainABI = toolchainVersion;
  parsed.payloadFormat = header.payloadFormat;
  parsed.vertEntries = std::move(newVertEntries);
  parsed.fragEntries = std::move(newFragEntries);
  _bundle = ShareParsedBundle(sourceHash, size, std::move(parsed));
  {
    std::lock_guard<std::mutex> lock(_programMissMutex);
    _programMisses.clear();
  }
  _generation.fetch_add(1, std::memory_order_relaxed);
  if (_context != nullptr) {
    // A new bundle generation invalidates every cached program: precompiled programs from the
    // replaced bundle must not be served under unchanged keys, and JIT programs must not keep
    // occupying keys the new bundle can now fill (which previously forced PrecompiledOnly
    // lookups to recreate their programs on every request).
    _context->globalCache()->clearPrograms();
  }
  LOGI("PrecompiledShaderCache: Loaded %u vert + %u frag entries (format v%u, profile=%s)",
       vertPoolCount, fragPoolCount, formatVersion, _bundle->profileTag.c_str());
  return true;
}

bool PrecompiledShaderCache::loadBundle(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }
  auto fileLength = file.tellg();
  if (fileLength < static_cast<std::streamoff>(BundleFormat::HeaderSize) ||
      fileLength > static_cast<std::streamoff>(MAX_BUNDLE_BYTES)) {
    return false;
  }
  auto fileSize = static_cast<size_t>(fileLength);
  file.seekg(0);
  std::vector<uint8_t> data(fileSize);
  if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize))) {
    return false;
  }
  return loadBundle(data.data(), fileSize);
}

bool PrecompiledShaderCache::findProgramMiss(const BytesKey& key,
                                             PrecompiledFallbackReason* reason) const {
  std::lock_guard<std::mutex> lock(_programMissMutex);
  auto it = _programMisses.find(key);
  if (it == _programMisses.end()) {
    return false;
  }
  if (reason != nullptr) {
    *reason = it->second;
  }
  return true;
}

void PrecompiledShaderCache::recordProgramMiss(const BytesKey& key,
                                               PrecompiledFallbackReason reason) {
  std::lock_guard<std::mutex> lock(_programMissMutex);
  _programMisses.emplace(key, reason);
}

void PrecompiledShaderCache::recordCachedMiss(PrecompiledFallbackReason reason) {
  if (statsRecordingPaused() || missPaused.load(std::memory_order_relaxed)) {
    return;
  }
  _missCount.fetch_add(1, std::memory_order_relaxed);
  auto index = static_cast<size_t>(reason);
  if (index < fallbackCounts.size()) {
    fallbackCounts[index].fetch_add(1, std::memory_order_relaxed);
  }
}

const ShaderStageBlob* PrecompiledShaderCache::findVertex(uint64_t hashHi, uint64_t hashLo) const {
  if (_bundle == nullptr) {
    return nullptr;
  }
  HashKey key{hashHi, hashLo};
  auto it = _bundle->vertEntries.find(key);
  if (it == _bundle->vertEntries.end()) {
    return nullptr;
  }
  return &it->second;
}

const ShaderStageBlob* PrecompiledShaderCache::findFragment(uint64_t hashHi,
                                                            uint64_t hashLo) const {
  if (_bundle == nullptr) {
    return nullptr;
  }
  HashKey key{hashHi, hashLo};
  auto it = _bundle->fragEntries.find(key);
  if (it == _bundle->fragEntries.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace tgfx

namespace tgfx {

namespace {

void AppendEscaped(std::string* out, const std::string& value) {
  out->push_back('"');
  for (char ch : value) {
    switch (ch) {
      case '"':
        out->append("\\\"");
        break;
      case '\\':
        out->append("\\\\");
        break;
      case '\n':
        out->append("\\n");
        break;
      default:
        out->push_back(ch);
        break;
    }
  }
  out->push_back('"');
}

}  // namespace

bool PrecompiledShaderCache::writeDiagnosticsDump(const std::string& path) const {
  auto stats = drawStats();
  auto misses = fallbackRecords();
  auto hits = hitRecords();
  std::string out;
  out.reserve(4096 + misses.size() * 512);
  out.append("{\n\"schemaVersion\": 2,\n\"identity\": {");
  out.append("\"backend\": " + std::to_string(static_cast<int>(_backend)));
  out.append(", \"profileTag\": ");
  AppendEscaped(&out, _bundle != nullptr ? _bundle->profileTag : std::string());
  out.append(", \"bundleLoaded\": ");
  out.append(isLoaded() ? "true" : "false");
  out.append("},\n\"drawMetrics\": {");
  out.append("\"draws\": " + std::to_string(stats.draws));
  out.append(", \"completeAOTDraws\": " + std::to_string(stats.completeAOTDraws));
  out.append(", \"directChainDraws\": " + std::to_string(stats.directChainDraws));
  out.append(", \"offscreenPlanDraws\": " + std::to_string(stats.offscreenPlanDraws));
  out.append(", \"droppedDraws\": " + std::to_string(stats.droppedDraws));
  out.append(", \"materializedEdges\": " + std::to_string(stats.materializedEdges));
  out.append(", \"renderTargetSwitches\": " + std::to_string(stats.renderTargetSwitches));
  out.append(", \"planPassHistogram\": [");
  for (size_t index = 0; index < stats.planPassHistogram.size(); ++index) {
    if (index != 0) {
      out.append(", ");
    }
    out.append(std::to_string(stats.planPassHistogram[index]));
  }
  out.append("]},\n");
  // Misses grouped by coarse reason plus the sub-reason that actually rejected the draw. That is
  // the whole point: NoMatchingRule alone cannot say whether the GL gate, a subset attribute, the
  // gradient budget or the enumerated set refused it.
  struct MissKey {
    int reason = 0;
    int failure = 0;
    bool operator<(const MissKey& other) const {
      return std::tie(reason, failure) < std::tie(other.reason, other.failure);
    }
  };
  std::map<MissKey, uint64_t> summary;
  for (const auto& record : misses) {
    if (record.deliberate) {
      continue;
    }
    MissKey key;
    key.reason = static_cast<int>(record.reason);
    key.failure = record.matchFailure;
    summary[key] += 1;
  }
  out.append("\"missSummary\": [");
  bool first = true;
  for (const auto& entry : summary) {
    if (!first) {
      out.append(",");
    }
    first = false;
    out.append("\n  {\"reason\": ");
    AppendEscaped(&out, PrecompiledFallbackReasonName(
                            static_cast<PrecompiledFallbackReason>(entry.first.reason)));
    out.append(", \"matchFailure\": ");
    AppendEscaped(&out, PermutationMatchFailureName(
                            static_cast<PermutationMatchFailure>(entry.first.failure)));
    out.append(", \"count\": " + std::to_string(entry.second) + "}");
  }
  out.append("\n],\n\"missRecords\": [");
  first = true;
  for (const auto& record : misses) {
    if (record.deliberate) {
      continue;
    }
    if (!first) {
      out.append(",");
    }
    first = false;
    out.append("\n  {\"effect\": ");
    AppendEscaped(&out, record.effectSignature);
    out.append(", \"pipeline\": ");
    AppendEscaped(&out, record.pipelineSignature);
    out.append(", \"reason\": ");
    AppendEscaped(&out, PrecompiledFallbackReasonName(record.reason));
    out.append(", \"matchFailure\": ");
    AppendEscaped(&out, PermutationMatchFailureName(
                            static_cast<PermutationMatchFailure>(record.matchFailure)));
    out.append("}");
  }
  out.append("\n],\n\"hitSummary\": [");
  std::map<std::string, uint64_t> hitSummary;
  for (const auto& hit : hits) {
    hitSummary[hit.shaderName] += 1;
  }
  first = true;
  for (const auto& entry : hitSummary) {
    if (!first) {
      out.append(",");
    }
    first = false;
    out.append("\n  {\"shader\": ");
    AppendEscaped(&out, entry.first);
    out.append(", \"count\": " + std::to_string(entry.second) + "}");
  }
  out.append("\n]\n}\n");
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }
  file << out;
  return file.good();
}

}  // namespace tgfx
