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

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/Program.h"
#include "gpu/Uniform.h"

namespace tgfx {

struct ShaderStageBlob {
  std::vector<uint8_t> data;
  std::vector<Uniform> uniforms;
  std::vector<Uniform> samplers;
};

enum class PrecompiledFallbackReason : uint8_t {
  CacheNotLoaded,
  UnsupportedOutputSwizzle,
  NoMatchingRule,
  // The pipeline carries a texture that is not instantiated yet (a lazy/deferred proxy): the
  // runtime drops such content via a stub program by design, so this is not a coverage gap.
  DeferredTexture,
  VertexArtifactMissing,
  FragmentArtifactMissing,
  VertexModuleCreationFailed,
  FragmentModuleCreationFailed,
  PipelineCreationFailed,
  Unspecified,
  Count,
};

enum class PrecompiledAOTStage : uint8_t {
  Attempt,
  CacheAvailable,
  PermutationMatched,
  ArtifactsFound,
  VertexModuleCreated,
  FragmentModuleCreated,
  PipelineCreated,
  Count,
};

struct PrecompiledHitRecord {
  std::string programKey;
  std::string effectSignature;
  std::string pipelineSignature;
  std::string shaderName;
  uint32_t vertPermutationIndex = std::numeric_limits<uint32_t>::max();
  uint32_t fragPermutationIndex = std::numeric_limits<uint32_t>::max();
};

struct JITProgramRecord {
  std::string programKey;
};

struct PrecompiledFallbackRecord {
  std::string programKey;
  PrecompiledFallbackReason reason = PrecompiledFallbackReason::Unspecified;
  std::string effectSignature;
  std::string pipelineSignature;
  std::string shaderName;
  uint32_t vertPermutationIndex = std::numeric_limits<uint32_t>::max();
  uint32_t fragPermutationIndex = std::numeric_limits<uint32_t>::max();
  // Set when the miss was provoked on purpose (a test fixture verifying miss accounting); such
  // records are listed but excluded from the production coverage metrics.
  bool deliberate = false;
  // Populated only for NoMatchingRule misses while diagnostic recording is enabled; drives the
  // offline decomposition-coverage audit. Left at its all-Trivial default otherwise.
  AOTDecomposeAnalysis decomposeAnalysis = {};
  // Populated under the same conditions as decomposeAnalysis: the static verdict on whether
  // folding the coverage axis into the color chain at program-creation time would make this draw
  // matchable (the decomposed-program route). NotApplicable when there is nothing to fold.
  AOTFoldRouteOutcome foldRouteOutcome = AOTFoldRouteOutcome::NotApplicable;
};

const char* PrecompiledFallbackReasonName(PrecompiledFallbackReason reason);
const char* PrecompiledAOTStageName(PrecompiledAOTStage stage);

/**
 * Draw-level metrics for the bounded AOT decomposition path. Unlike the per-program-lookup counters
 * above, these describe how one logical Draw decomposes: how many Kernel invocations it issues, how
 * many offscreen targets and materialized edges it creates, and the intermediate/peak temporary
 * bytes it consumes. A Draw counts as a complete AOT Draw only when every one of its invocations
 * hits AOT (see design §9.2). Atomic fallbacks count planned AOT Draws that execute only the
 * untouched original Draw after strict preparation rejects the whole plan.
 */
enum class OffscreenFillSource : uint8_t {
  Unknown,
  FPFlatten,
  GaussianBlur,
  ImageFilter,
  TransformImage,
  RGBAAAImage,
  Count,
};

const char* OffscreenFillSourceName(OffscreenFillSource source);

using OffscreenFillKey = uint64_t;
static constexpr OffscreenFillKey InvalidOffscreenFillKey = 0;

struct OffscreenFillCorrelation {
  OffscreenFillSource source = OffscreenFillSource::Unknown;
  std::string topLevelProcessor = {};
  std::string kernelSignature = {};
  uint64_t calls = 0;
  uint64_t canExecute = 0;
  uint64_t precompiledPrograms = 0;
  uint64_t programBuilderPrograms = 0;
  uint64_t precompiledCanExecute = 0;
  uint64_t programBuilderCanExecute = 0;
};

/// Diagnostic-only aggregate for fillRTWithFP() calls. Static planning fields are sampled before
/// the processor is moved into a DrawOp; runtime provenance is recorded after DrawOp preparation.
struct OffscreenFillStats {
  uint64_t calls = 0;
  uint64_t coordOffsetNonZero = 0;
  uint64_t lowerSucceeded = 0;
  uint64_t validateSucceeded = 0;
  uint64_t decomposeSucceeded = 0;
  uint64_t canExecute = 0;
  uint64_t pointwiseChainPlans = 0;
  uint64_t pointwiseTailPlans = 0;
  uint64_t multiPassPlans = 0;
  uint64_t precompiledPrograms = 0;
  uint64_t programBuilderPrograms = 0;
  uint64_t precompiledCanExecute = 0;
  uint64_t programBuilderCanExecute = 0;
  std::unordered_map<std::string, uint64_t> lowerBlockers = {};
  std::unordered_map<std::string, uint64_t> topLevelProcessors = {};
};

struct AOTDrawStats {
  uint64_t draws = 0;
  uint64_t completeAOTDraws = 0;
  uint64_t atomicFallbacks = 0;
  uint64_t kernelInvocations = 0;
  uint64_t offscreenTargets = 0;
  uint64_t materializedEdges = 0;
  uint64_t renderTargetSwitches = 0;
  uint64_t intermediateReadBytes = 0;
  uint64_t intermediateWriteBytes = 0;
  uint64_t peakTemporaryBytes = 0;
};

/// Runtime cache that loads precompiled shader bundles and provides O(1) lookup by ShaderKey hash.
/// Each Context holds one instance. Bundle v3 stores vertex and fragment shaders in separate pools,
/// enabling M+N storage instead of M*N.
class PrecompiledShaderCache {
 public:
  /// Loads a bundle file from the given path. On success, the bundle completely replaces any
  /// previously loaded entries. On failure, the existing cache remains unchanged.
  bool loadBundle(const std::string& path);

  /// Loads a bundle from in-memory data. On success, the bundle completely replaces any previously
  /// loaded entries. On failure, the existing cache remains unchanged. The caller must ensure the
  /// data remains valid until parsing completes. This overload is used for embedded bundles
  /// compiled into the library.
  bool loadBundle(const uint8_t* data, size_t size);

  /// Looks up a vertex shader by its 128-bit hash. Returns nullptr if not found.
  const ShaderStageBlob* findVertex(uint64_t hashHi, uint64_t hashLo) const;

  /// Looks up a fragment shader by its 128-bit hash. Returns nullptr if not found.
  const ShaderStageBlob* findFragment(uint64_t hashHi, uint64_t hashLo) const;

  /// Returns true if at least one bundle has been loaded.
  bool isLoaded() const {
    return !vertEntries.empty() || !fragEntries.empty();
  }

  /// Returns the total number of loaded entries (vertex + fragment).
  size_t entryCount() const {
    return vertEntries.size() + fragEntries.size();
  }

  /// Returns the number of vertex shader entries.
  size_t vertexEntryCount() const {
    return vertEntries.size();
  }

  /// Returns the number of fragment shader entries.
  size_t fragmentEntryCount() const {
    return fragEntries.size();
  }

  /// Returns the profile tag from the loaded bundle header (e.g. "vulkan-android").
  const std::string& profileTag() const {
    return _profileTag;
  }

  /// Returns the number of successful lookups where both vertex and fragment artifacts were found.
  uint32_t hitCount() const {
    return _hitCount.load(std::memory_order_relaxed);
  }

  /// Returns the number of failed artifact lookups.
  uint32_t missCount() const {
    return _missCount.load(std::memory_order_relaxed);
  }

  /// Records that both vertex and fragment artifacts were found for one AOT attempt.
  void recordArtifactHit();

  /// Records one artifact-lookup miss without assigning a more specific reason.
  void recordMiss() {
    recordArtifactMiss(PrecompiledFallbackReason::Unspecified);
  }

  /// Records one artifact-lookup miss using a fixed low-cardinality reason.
  void recordArtifactMiss(PrecompiledFallbackReason reason,
                          const PrecompiledFallbackRecord& record = {});

  /// Records a module or pipeline failure without changing artifact lookup counters.
  void recordFailure(PrecompiledFallbackReason reason,
                     const PrecompiledFallbackRecord& record = {});

  /// Records successful progress through one stage of the AOT program-creation pipeline. A
  /// PipelineCreated event optionally stores its full diagnostic record when recording is enabled.
  void recordAOTStage(PrecompiledAOTStage stage, const PrecompiledHitRecord& record = {});

  /// Returns the number of AOT attempts that successfully completed the specified stage.
  uint32_t aotStageCount(PrecompiledAOTStage stage) const {
    auto index = static_cast<size_t>(stage);
    if (index >= aotStageCounts.size()) {
      return 0;
    }
    return aotStageCounts[index].load(std::memory_order_relaxed);
  }

  /// Returns the number of failed attempts for the specified fallback reason.
  uint32_t fallbackCount(PrecompiledFallbackReason reason) const {
    auto index = static_cast<size_t>(reason);
    if (index >= fallbackCounts.size()) {
      return 0;
    }
    return fallbackCounts[index].load(std::memory_order_relaxed);
  }

  /// Enables or disables detailed fallback recording without affecting aggregate counters.
  void setDiagnosticRecordingEnabled(bool enabled) {
    diagnosticsEnabled.store(enabled, std::memory_order_relaxed);
  }

  bool diagnosticRecordingEnabled() const {
    return diagnosticsEnabled.load(std::memory_order_relaxed);
  }

  /// Pauses or resumes all statistics recording. While paused every record* method is a no-op, so
  /// intentional JIT renders (e.g. cross-validation reference passes with the bundle unloaded) do
  /// not pollute the AOT hit-rate accounting with draws that are not production lookups.
  // Pauses only artifact-miss recording (route-validation lookups use this: their failure is a
  // route mismatch, not a user-pipeline miss, and the fallback path records the original
  // pipeline instead). Hit and stage accounting stay live.
  void setMissRecordingPaused(bool paused) {
    missPaused.store(paused, std::memory_order_relaxed);
  }

  /// While set, artifact-miss records are stamped as deliberate (provoked on purpose by a test
  /// fixture). They stay listed but are excluded from the coverage metrics.
  void setDeliberateMissMarking(bool marking) {
    deliberateMarking.store(marking, std::memory_order_relaxed);
  }

  void setStatsRecordingPaused(bool paused) {
    statsPaused.store(paused, std::memory_order_relaxed);
  }

  bool statsRecordingPaused() const {
    return statsPaused.load(std::memory_order_relaxed);
  }

  /// Enables or disables the bounded-AOT decomposition route. Off by default: the decomposition
  /// executor is served only when explicitly enabled (e.g. during cross-validation), so unverified
  /// kernels never reach production and every draw falls back to the plain matcher/builder path.
  void setDecompositionEnabled(bool enabled) {
    _decompositionEnabled.store(enabled, std::memory_order_relaxed);
  }

  bool decompositionEnabled() const {
    return _decompositionEnabled.load(std::memory_order_relaxed);
  }

  /// Returns a snapshot of successful AOT pipeline records collected since the last reset.
  std::vector<PrecompiledHitRecord> hitRecords() const;

  /// Returns a snapshot of detailed fallback records collected since the last reset.
  std::vector<PrecompiledFallbackRecord> fallbackRecords() const;
  void recordJITProgram(const JITProgramRecord& record);
  std::vector<JITProgramRecord> jitProgramRecords() const;

  /// Records the decomposition outcome of one logical Draw. `complete` marks whether every Kernel
  /// invocation of this Draw hit AOT. Called by the decomposition executor (stage 2+).
  void recordDraw(const AOTDrawStats& delta, bool complete);

  /// Records one materialized edge produced outside the decomposition executor, i.e. by an
  /// inline flatten that renders an FP subtree into an offscreen texture. Unlike recordDraw this
  /// leaves the Draw-level counters untouched, because an inline flatten is one edge of a Draw
  /// rather than a Draw of its own. `bytes` is the size of the offscreen target it allocated.
  void recordMaterializedEdge(uint64_t bytes);

  /// Returns a snapshot of accumulated Draw-level metrics since the last reset.
  AOTDrawStats drawStats() const;

  OffscreenFillKey recordOffscreenFillAnalysis(OffscreenFillSource source, bool coordOffsetNonZero,
                                               const std::string& topLevelProcessor,
                                               bool lowerSucceeded, const std::string& lowerBlocker,
                                               bool validateSucceeded, bool decomposeSucceeded,
                                               bool canExecute,
                                               const std::vector<AOTKernelKind>& kernels);
  void recordOffscreenFillProgram(OffscreenFillKey key, ProgramOrigin origin);
  OffscreenFillStats offscreenFillStats() const;
  std::vector<OffscreenFillCorrelation> offscreenFillCorrelations() const;
  std::array<OffscreenFillStats, static_cast<size_t>(OffscreenFillSource::Count)>
  offscreenFillStatsBySource() const;

  /// Resets AOT stages, artifact lookups, fallback counters, detailed records, and Draw-level
  /// metrics to zero.
  void resetStats();

  /// Unloads all bundle entries without discarding diagnostic statistics from the current session.
  void unload();

  struct HashKey {
    uint64_t hi;
    uint64_t lo;
    bool operator==(const HashKey& other) const {
      return hi == other.hi && lo == other.lo;
    }
  };

  struct HashKeyHasher {
    size_t operator()(const HashKey& k) const {
      return static_cast<size_t>(k.hi ^ (k.lo * 0x9E3779B97F4A7C15ULL));
    }
  };

 private:
  std::string _profileTag;
  std::unordered_map<HashKey, ShaderStageBlob, HashKeyHasher> vertEntries;
  std::unordered_map<HashKey, ShaderStageBlob, HashKeyHasher> fragEntries;
  std::atomic<uint32_t> _hitCount{0};
  std::atomic<uint32_t> _missCount{0};
  std::array<std::atomic<uint32_t>, static_cast<size_t>(PrecompiledAOTStage::Count)>
      aotStageCounts = {};
  std::array<std::atomic<uint32_t>, static_cast<size_t>(PrecompiledFallbackReason::Count)>
      fallbackCounts = {};
  std::atomic<bool> diagnosticsEnabled{false};
  std::atomic<bool> statsPaused{false};
  std::atomic<bool> missPaused{false};
  std::atomic<bool> deliberateMarking{false};
  std::atomic<bool> _decompositionEnabled{true};
  mutable std::mutex diagnosticsMutex = {};
  std::vector<PrecompiledHitRecord> _hitRecords = {};
  std::vector<PrecompiledFallbackRecord> _fallbackRecords = {};
  std::vector<JITProgramRecord> _jitProgramRecords = {};
  mutable std::mutex drawStatsMutex = {};
  AOTDrawStats _drawStats = {};
  struct OffscreenFillStaticRecord {
    OffscreenFillSource source = OffscreenFillSource::Unknown;
    std::string topLevelProcessor = {};
    std::string kernelSignature = {};
    bool canExecute = false;
  };
  mutable std::mutex offscreenFillStatsMutex = {};
  std::atomic<OffscreenFillKey> _nextOffscreenFillKey{1};
  std::unordered_map<OffscreenFillKey, OffscreenFillStaticRecord> _offscreenFillRecords = {};
  std::unordered_map<std::string, OffscreenFillCorrelation> _offscreenFillCorrelations = {};
  std::array<OffscreenFillStats, static_cast<size_t>(OffscreenFillSource::Count)>
      _offscreenFillStats = {};
};

}  // namespace tgfx
