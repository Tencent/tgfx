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
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/Program.h"
#include "gpu/ShaderCache.h"
#include "gpu/Uniform.h"
#include "gpu/UniformData.h"
#include "gpu/aot/bundle/BundleData.h"
#include "gpu/aot/plan/AOTEffectDecomposer.h"
#include "gpu/aot/spec/SpecSource.h"
#include "gpu/shaders/contract/AOTFailure.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/Backend.h"

namespace tgfx {

class Context;
enum class AOTDecomposeOutcome;

// The bundle data model (stages, keys, the parsed bundle) lives in BundleData.h: the reader that
// fills it and this cache that stores it are separate translation units.

/// What happened to the bundle embedded in the library when a Context was created.
enum class EmbeddedBundleState : uint8_t {
  /// The Context did not try to load one (or the cache was built standalone).
  NotAttempted,
  /// No bundle is embedded for the backend.
  NoBundleForBackend,
  /// A bundle is embedded and the cache refused it.
  Rejected,
  /// A bundle is embedded and was loaded.
  Loaded,
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

/// The fields every program-creation record carries: the key an issue reproduces by, the two
/// signatures a diagnostic groups by, and the matched family variant. Shared by the hit and the
/// fallback records so a new diagnostic field lands once.
struct PrecompiledProgramRecord {
  std::string programKey;
  std::string effectSignature;
  std::string pipelineSignature;
  std::string shaderName;
  uint32_t vertPermutationIndex = std::numeric_limits<uint32_t>::max();
  uint32_t fragPermutationIndex = std::numeric_limits<uint32_t>::max();
};

struct PrecompiledHitRecord : PrecompiledProgramRecord {};

struct PrecompiledFallbackRecord : PrecompiledProgramRecord {
  PrecompiledFallbackReason reason = PrecompiledFallbackReason::Unspecified;
  // Sub-reason of the miss as a PermutationMatchFailure value (0 = None). NoMatchingRule alone
  // cannot say whether the GL gate, a subset attribute, the gradient budget or the enumerated
  // variant set rejected the draw.
  uint8_t matchFailure = 0;
  // Set when the miss was provoked on purpose (a test fixture verifying miss accounting); such
  // records are listed but excluded from the production coverage metrics.
  bool deliberate = false;
  // Populated only for NoMatchingRule misses while diagnostic recording is enabled; drives the
  // offline decomposition-coverage audit. Left at its all-Trivial default otherwise.
  AOTDecomposeAnalysis decomposeAnalysis = {};
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
  // Route-level counters: each draw is counted once by the route that finally served it, either
  // the direct decomposition rewrite in StandardDrawOp or the atomic multi-pass plan task.
  uint64_t directChainDraws = 0;
  uint64_t offscreenPlanDraws = 0;
  // F12 execution-failure diagnostics: a plan task that passed the prepare phase (all programs
  // resolved, all targets instantiated) but lost an intermediate or terminal pass during
  // execution (beginRenderPass failed). Such a task stops after the failure — the final target
  // may carry partial passes — so the count is the observable "why did this draw not land"
  // signal, not a recoverable event.
  uint64_t planExecutionFailures = 0;
  // Draws that rendered nothing because their final program lookup failed (with a bundle loaded:
  // no precompiled program serves them and there is no runtime fallback). Every one is also logged
  // as an error. Drops provoked on purpose inside a deliberate-miss scope (test fixtures) are
  // counted in deliberateDroppedDraws instead.
  uint64_t droppedDraws = 0;
  uint64_t deliberateDroppedDraws = 0;
  // Distribution of executed plan pass counts: bucket k counts plans with k+1 passes, the last
  // bucket accumulates deeper plans.
  std::array<uint64_t, 4> planPassHistogram = {};
  // materializedEdges split by phase: construction-time blend-child flattening versus plan-time
  // intermediate passes.
  uint64_t fpFlattenEdges = 0;
  uint64_t planMaterializedEdges = 0;
  // Draws whose color chain the decomposition route attempted and refused, bucketed by
  // AOTDecomposeOutcome (index == static_cast<size_t>(outcome)). Recorded through
  // recordDecomposeRejection() only when diagnostic recording is enabled, so a draw that
  // silently fell back to the plain route carries an observable "why" instead of nothing.
  // FusablePointwise never appears here: that outcome means the route was not attempted.
  std::array<uint64_t, 4> decomposeRejections = {};
};

/// Runtime cache that loads precompiled shader bundles and provides O(1) lookup by ShaderKey hash.
/// Each Context holds one instance. The bundle stores vertex and fragment shaders in separate
/// pools, enabling M+N storage instead of M*N.
class PrecompiledShaderCache : public SpecSource {
 public:
  /// The cache is owned by its Context and keeps a back pointer for bundle-generation
  /// bookkeeping: every load or unload bumps the generation and invalidates cached programs.
  /// The backend is captured at construction because the bundle's profile tag must be verified
  /// during the Context constructor (embedded-bundle auto load), before the owner-thread
  /// contract is in effect and Context::backend() may be called.
  explicit PrecompiledShaderCache(Context* context, Backend backend);

  /// Returns the number of bundle generation changes (loads and unloads) since construction.
  /// Diagnostic counter for lifecycle verification; program invalidation itself happens
  /// eagerly at each generation change.
  uint64_t bundleGeneration() const {
    return _generation.load(std::memory_order_relaxed);
  }

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

  /// What became of the embedded bundle when the owning Context loaded it. Set once by the Context;
  /// later loadBundle()/unload() calls (tests swap bundles in and out) do not change it. It only
  /// tells a draw that finds the cache empty WHY: see PrecompiledFallbackReason.
  EmbeddedBundleState embeddedBundleState() const {
    return _embeddedBundleState;
  }

  void setEmbeddedBundleState(EmbeddedBundleState state) {
    _embeddedBundleState = state;
  }

  /// What the loaded bundle's code blobs hold, one of BundleFormat::Payload. Reports the text form
  /// before any bundle is loaded.
  uint16_t payloadFormat() const override {
    return _bundle == nullptr ? BundleFormat::Payload::Text : _bundle->payloadFormat;
  }

  /// Returns true if at least one bundle has been loaded.
  bool isLoaded() const override {
    return _bundle != nullptr && (!_bundle->vertEntries.empty() || !_bundle->fragEntries.empty());
  }

  /// Returns the total number of loaded entries (vertex + fragment).
  size_t entryCount() const {
    return _bundle == nullptr ? 0 : _bundle->vertEntries.size() + _bundle->fragEntries.size();
  }

  /// Returns the number of vertex shader entries.
  size_t vertexEntryCount() const {
    return _bundle == nullptr ? 0 : _bundle->vertEntries.size();
  }

  /// Returns the number of fragment shader entries.
  size_t fragmentEntryCount() const {
    return _bundle == nullptr ? 0 : _bundle->fragEntries.size();
  }

  /// Returns the profile tag from the loaded bundle header (e.g. "vulkan"; see
  /// ShaderBundleContract.h).
  const std::string& profileTag() const override {
    static const std::string empty;
    return _bundle == nullptr ? empty : _bundle->profileTag;
  }

  /// Returns the content identity hash recorded in the loaded bundle's header. Read-only
  /// diagnostics for report binding: it identifies WHICH bundle bytes a run consumed, so a report
  /// can be attributed to an exact artifact rather than a presumed one.
  uint64_t bundleIdentityHash() const {
    return _bundle == nullptr ? 0 : _bundle->identityHash;
  }

  /// Returns the toolchain ABI version of the loaded bundle (0 when no bundle is loaded).
  uint32_t bundleToolchainABI() const {
    return _bundle == nullptr ? 0 : _bundle->toolchainABI;
  }

  /// Records a program key the precompiled route refused, with the reason: later draws carrying
  /// the same key are refused without walking the permutation rules again. Cleared whenever the
  /// bundle changes, because a new bundle may serve a key the old one refused.
  bool findProgramMiss(const BytesKey& key, PrecompiledFallbackReason* reason) const;
  void recordProgramMiss(const BytesKey& key, PrecompiledFallbackReason reason);

  /// Counts a miss served from the negative cache: the cheap counters move, but the per-draw
  /// fallback record (which formats strings) is not built again.
  void recordCachedMiss(PrecompiledFallbackReason reason);

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

  bool deliberateMissMarking() const {
    return deliberateMarking.load(std::memory_order_relaxed);
  }

  void setStatsRecordingPaused(bool paused) {
    statsPaused.store(paused, std::memory_order_relaxed);
  }

  bool statsRecordingPaused() const {
    return statsPaused.load(std::memory_order_relaxed);
  }

  /// Enables or disables the bounded-AOT decomposition route on every entry point (direct draw
  /// rewrite, offscreen fill, and the compositor's on-screen plans). On by default: the
  /// decomposition route is part of the precompiled execution path. Turning it off retains
  /// required gradient lowering but otherwise uses the plain matcher, which is useful for comparisons and
  /// troubleshooting; note that bundle loading itself is controlled separately by loadBundle.
  void setDecompositionEnabled(bool enabled) {
    _decompositionEnabled.store(enabled, std::memory_order_relaxed);
  }

  bool decompositionEnabled() const {
    return _decompositionEnabled.load(std::memory_order_relaxed);
  }

  /// Diagnostic switch. On by default: a draw that a dedicated kernel family serves keeps that
  /// family. Turning it off lets such a draw be rewritten onto the pointwise chain kernel, so a
  /// test can run the same draw through both routes and compare them.
  void setDedicatedKernelsEnabled(bool enabled) {
    _dedicatedKernelsEnabled.store(enabled, std::memory_order_relaxed);
  }

  bool dedicatedKernelsEnabled() const {
    return _dedicatedKernelsEnabled.load(std::memory_order_relaxed);
  }

  /// The decisions a consumer branches on, captured once: whether a bundle is loaded and the two
  /// diagnostic gates. A matcher pass reads this value instead of asking the cache per family.
  struct AOTPolicy {
    bool bundleLoaded = false;
    bool dedicatedKernels = true;
    bool decomposition = true;
  };

  AOTPolicy aotPolicy() const {
    return AOTPolicy{isLoaded(), _dedicatedKernelsEnabled.load(std::memory_order_relaxed),
                     _decompositionEnabled.load(std::memory_order_relaxed)};
  }

  /// Returns a snapshot of successful AOT pipeline records collected since the last reset.
  std::vector<PrecompiledHitRecord> hitRecords() const;

  /// Returns a snapshot of detailed fallback records collected since the last reset.
  std::vector<PrecompiledFallbackRecord> fallbackRecords() const;

  /// Records the decomposition outcome of one logical Draw. `complete` marks whether every Kernel
  /// invocation of this Draw hit AOT. Called by the decomposition executor (stage 2+).
  void recordDraw(const AOTDrawStats& delta, bool complete);

  /**
   * Counts a draw dropped because its final program lookup failed (AOTDrawStats::droppedDraws).
   */
  void recordDroppedDraw();

  /// Settles the simultaneously-live materialized bytes accumulated by recordActiveTemporaryBytes
  /// into peakTemporaryBytes and resets the running total. Called once per flush cycle, before the
  /// recorded draws execute: the settled value is the maximum set of offscreen bytes that coexisted
  /// while the frame was being built.
  void settleActiveTemporaryBytes();

  /// Adds `bytes` to the simultaneously-live running total without touching any per-edge or
  /// per-draw counter — for an offscreen plan's intermediate render targets, counted at task
  /// creation so they settle inside the recording cycle; the task's own recordDraw delta is a
  /// different, draw-local view of the same bytes.
  void recordActiveTemporaryBytes(uint64_t bytes);

  /// Records one draw whose color chain the decomposition route attempted and refused, with the
  /// pure-analysis reason (AOTDecomposeOutcome). Diagnostic-only, like the rest of the draw
  /// stats: paused stats drop it, and it never influences routing.
  void recordDecomposeRejection(AOTDecomposeOutcome outcome);

  /// Returns a snapshot of accumulated Draw-level metrics since the last reset.
  AOTDrawStats drawStats() const;

  /// Resets AOT stages, artifact lookups, fallback counters, detailed records, and Draw-level
  /// metrics to zero.
  void resetStats();

  /// Returns the cache of compiled shader modules. It lives here because this object exists for
  /// every Context and backend, including those with no bundle; it serves the precompiled
  /// creator's module and pipeline creation.
  ShaderCache* shaderCache() {
    return &_shaderCache;
  }

  /// Whether the AOT route may specialize a precompiled variant with the draw's structural
  /// uniform values before creating its program. On by default, and the resolver's format gate
  /// keeps it to the backends whose stored form can consume the values (GLSL text rewrite, Metal
  /// function constants); TGFX_AOT_SPECIALIZE=0 turns it off for a whole run.
  bool specializationEnabled() const override {
    return _specializationEnabled;
  }

  void setSpecializationEnabled(bool enabled) {
    _specializationEnabled = enabled;
  }

  /// What the specialization route paid for: how many resolves returned values, and how many
  /// distinct value sets those resolves covered. The second number is the compile count a
  /// fully-specialized runtime pays for one run of a scene on a cold pipeline cache, so the cost
  /// audit compares it between runs with TGFX_AOT_SPECIALIZE off and on.
  struct SpecStats {
    uint64_t captures = 0;
    uint64_t distinctStructures = 0;
  };

  /// Records one resolved specialization. An empty value set is the generic form and is ignored.
  void recordSpecCapture(const SpecValues& values) override;

  /// Returns a snapshot of the specialization accounting since the last resetStats().
  SpecStats specStats() const;

  /// The specialization capture's per-variant scratch: uniform data backed by CPU buffers, so
  /// the capture runs the very writers the upload runs without touching any GPU object. One
  /// scratch per fragment stage key, rebuilt whenever the bundle changes.
  /// Returns the capture scratch of one (vertex, fragment) stage pair, creating it from the two
  /// stages' reflected uniforms on first use. Returns nullptr when either stage has no blob.
  SpecScratch* findOrCreateSpecScratch(uint64_t vertHi, uint64_t vertLo, uint64_t fragHi,
                                       uint64_t fragLo) override;

  /// SpecSource: the two stages' reflected layouts and the fragment stage's spec table, as the
  /// capture needs them. A missing stage yields null rather than an empty view.
  const std::vector<Uniform>* findVertexUniforms(uint64_t hashHi, uint64_t hashLo) const override;
  const std::vector<Uniform>* findFragmentUniforms(uint64_t hashHi, uint64_t hashLo) const override;
  const std::vector<SpecFieldInfo>* findFragmentSpecFields(uint64_t hashHi,
                                                           uint64_t hashLo) const override;

  using HashKey = BundleStageKey;

  using HashKeyHasher = BundleStageKeyHasher;

 private:
  struct SharedBundleKeyHasher {
    size_t operator()(const std::pair<uint64_t, size_t>& key) const {
      return static_cast<size_t>(key.first ^ (key.second * 0x9E3779B97F4A7C15ULL));
    }
  };

  /// The process-wide parsed-bundle registry: weak pointers keyed by (identity hash, byte size), so
  /// a bundle stays parsed while any cache holds it and is re-parsed only after every holder is
  /// gone.
  static std::unordered_map<std::pair<uint64_t, size_t>, std::weak_ptr<const ParsedBundle>,
                            SharedBundleKeyHasher>&
  SharedBundleRegistry();

  /// Returns the process-shared parse for (identityHash, size), or nullptr.
  static std::shared_ptr<const ParsedBundle> FindSharedBundle(uint64_t identityHash, size_t size);

  /// Returns the process-shared parse for (identityHash, size), registering `parsed` when this is
  /// the first. `parsed` is consumed only on a miss.
  static std::shared_ptr<const ParsedBundle> ShareParsedBundle(uint64_t identityHash, size_t size,
                                                               ParsedBundle parsed);

  Context* _context = nullptr;
  Backend _backend = Backend::Unknown;
  ShaderCache _shaderCache;
  std::atomic<uint64_t> _generation{0};
  /// The specialization capture's per-(vertex, fragment)-stage scratch, keyed by the two stage
  /// hashes and dropped whenever a bundle load replaces the stage blobs (the reflected uniforms
  /// the scratch was built from may no longer be the same).
  std::map<std::pair<HashKey, HashKey>, std::unique_ptr<SpecScratch>> _specScratch;
  bool _specializationEnabled = false;
  mutable std::mutex _specStatsMutex;
  SpecStats _specStats = {};
  // The distinct value sets themselves rather than a digest: the audit reports a compile count,
  // and a collision would understate it.
  std::set<std::vector<int32_t>> _specStructures;
  std::shared_ptr<const ParsedBundle> _bundle = nullptr;
  mutable std::mutex _programMissMutex;
  std::unordered_map<BytesKey, PrecompiledFallbackReason, BytesKeyHasher> _programMisses;
  // Simultaneously-live materialized-texture accounting (peakTemporaryBytes): materialization
  // edges created during a flush cycle (blend-child flattens, offscreen plan targets) add their
  // bytes to this running total; flush() settles it into peakTemporaryBytes and resets. The
  // resulting peak is the maximum set of offscreen bytes coexisting within one flush cycle —
  // the honest footprint of a full-effect frame (a draw's materialized children plus any plan
  // intermediates), instead of the previous zero that ignored construction-time materialization
  // while the traffic counters recorded hundreds of megabytes over the same edges.
  uint64_t _activeTemporaryBytes = 0;
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
  std::atomic<bool> _dedicatedKernelsEnabled{true};
  EmbeddedBundleState _embeddedBundleState = EmbeddedBundleState::NotAttempted;
  mutable std::mutex diagnosticsMutex = {};
  std::vector<PrecompiledHitRecord> _hitRecords = {};
  std::vector<PrecompiledFallbackRecord> _fallbackRecords = {};
  mutable std::mutex drawStatsMutex = {};
  AOTDrawStats _drawStats = {};
};

}  // namespace tgfx
