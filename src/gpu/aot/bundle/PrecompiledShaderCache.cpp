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
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/ShaderCaps.h"
#include "gpu/aot/AOTDiagnostics.h"
#include "gpu/aot/bundle/BundleReader.h"
#include "gpu/shaders/BundleCodec.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gpu/shaders/contract/ShaderProfileResolver.h"
#include "tgfx/gpu/Context.h"
#include "zstd.h"

namespace tgfx {

namespace {

// Specialization is on by default. The resolver's format gate decides which backends can consume
// the values — today the GLSL text rewrite and Metal's function constants; a backend without an
// injector keeps the generic form regardless of this switch. TGFX_AOT_SPECIALIZE=0 turns it off
// for a run, which the cost audit and the A/B tests use.
bool SpecializationRequestedByEnvironment() {
  const char* value = std::getenv("TGFX_AOT_SPECIALIZE");
  if (value == nullptr) {
    return true;
  }
  std::string text = value;
  return text != "0" && text != "false" && text != "off";
}

}  // namespace

PrecompiledShaderCache::PrecompiledShaderCache(Context* context, Backend backend)
    : _context(context), _backend(backend), _shaderCache(context),
      _specializationEnabled(SpecializationRequestedByEnvironment()) {
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

void RecordActiveTemporaryBytes(Context* context, uint64_t bytes) {
  if (context == nullptr) {
    return;
  }
  auto* cache = ContextCacheAccess::Cache(context);
  if (cache != nullptr) {
    cache->recordActiveTemporaryBytes(bytes);
  }
}

AOTDrawStats PrecompiledShaderCache::drawStats() const {
  std::lock_guard<std::mutex> autoLock(drawStatsMutex);
  return _drawStats;
}

void PrecompiledShaderCache::recordSpecCapture(const SpecValues& values) {
  if (values.empty()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(_specStatsMutex);
  _specStats.captures++;
  if (_specStructures.insert(values.values).second) {
    _specStats.distinctStructures++;
  }
}

PrecompiledShaderCache::SpecStats PrecompiledShaderCache::specStats() const {
  std::lock_guard<std::mutex> autoLock(_specStatsMutex);
  return _specStats;
}

void PrecompiledShaderCache::resetStats() {
  _hitCount.store(0, std::memory_order_relaxed);
  _missCount.store(0, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> specLock(_specStatsMutex);
    _specStats = {};
    _specStructures.clear();
  }
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

// The header and pool entry layout is defined in ShaderBundleContract.h, shared with the bundle writer.
// Independent bounds for the input/reassembled file and for copied shader/reflection payloads.
// These are parser limits, not a bound on total process RSS or concurrently retained old bundles.
static constexpr size_t MAX_BUNDLE_BYTES = 256u * 1024u * 1024u;
static constexpr size_t MAX_DECODED_PAYLOAD_BYTES = 256u * 1024u * 1024u;
static constexpr uint32_t MAX_BUNDLE_ENTRIES = 65536;

std::unordered_map<std::pair<uint64_t, size_t>, std::weak_ptr<const ParsedBundle>,
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

std::shared_ptr<const ParsedBundle> PrecompiledShaderCache::FindSharedBundle(uint64_t identityHash,
                                                                             size_t size) {
  std::lock_guard<std::mutex> lock(SharedBundleRegistryMutex());
  auto it = SharedBundleRegistry().find({identityHash, size});
  return it == SharedBundleRegistry().end() ? nullptr : it->second.lock();
}

std::shared_ptr<const ParsedBundle> PrecompiledShaderCache::ShareParsedBundle(uint64_t identityHash,
                                                                              size_t size,
                                                                              ParsedBundle parsed) {
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

const std::vector<SpecFieldInfo>* PrecompiledShaderCache::findFragmentSpecFields(
    uint64_t hashHi, uint64_t hashLo) const {
  auto* blob = findFragment(hashHi, hashLo);
  return blob == nullptr || blob->specFields.empty() ? nullptr : &blob->specFields;
}

}  // namespace tgfx
