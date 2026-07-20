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
  std::string effectSignature;
  std::string pipelineSignature;
  std::string shaderName;
  uint32_t vertPermutationIndex = std::numeric_limits<uint32_t>::max();
  uint32_t fragPermutationIndex = std::numeric_limits<uint32_t>::max();
};

struct PrecompiledFallbackRecord {
  PrecompiledFallbackReason reason = PrecompiledFallbackReason::Unspecified;
  std::string effectSignature;
  std::string pipelineSignature;
  std::string shaderName;
  uint32_t vertPermutationIndex = std::numeric_limits<uint32_t>::max();
  uint32_t fragPermutationIndex = std::numeric_limits<uint32_t>::max();
};

const char* PrecompiledFallbackReasonName(PrecompiledFallbackReason reason);
const char* PrecompiledAOTStageName(PrecompiledAOTStage stage);

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

  /// Returns a snapshot of successful AOT pipeline records collected since the last reset.
  std::vector<PrecompiledHitRecord> hitRecords() const;

  /// Returns a snapshot of detailed fallback records collected since the last reset.
  std::vector<PrecompiledFallbackRecord> fallbackRecords() const;

  /// Resets AOT stages, artifact lookups, fallback counters, and detailed records to zero.
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
  mutable std::mutex diagnosticsMutex = {};
  std::vector<PrecompiledHitRecord> _hitRecords = {};
  std::vector<PrecompiledFallbackRecord> _fallbackRecords = {};
};

}  // namespace tgfx
