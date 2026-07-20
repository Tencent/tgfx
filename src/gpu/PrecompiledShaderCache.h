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
  NoPermutationMatch,
  VertexArtifactMissing,
  FragmentArtifactMissing,
  VertexModuleCreationFailed,
  FragmentModuleCreationFailed,
  PipelineCreationFailed,
  Unspecified,
  Count,
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

  /// Returns the number of successful lookups (both vertex and fragment found).
  uint32_t hitCount() const {
    return _hitCount.load(std::memory_order_relaxed);
  }

  /// Returns the number of failed artifact lookups.
  uint32_t missCount() const {
    return _missCount.load(std::memory_order_relaxed);
  }

  /// Increments the artifact-lookup hit counter by one.
  void recordHit() {
    _hitCount.fetch_add(1, std::memory_order_relaxed);
  }

  /// Records one artifact-lookup miss without assigning a more specific reason.
  void recordMiss() {
    recordArtifactMiss(PrecompiledFallbackReason::Unspecified);
  }

  /// Records one artifact-lookup miss using a fixed low-cardinality reason.
  void recordArtifactMiss(PrecompiledFallbackReason reason) {
    _missCount.fetch_add(1, std::memory_order_relaxed);
    recordFailure(reason);
  }

  /// Records a module or pipeline failure without changing artifact lookup counters.
  void recordFailure(PrecompiledFallbackReason reason) {
    auto index = static_cast<size_t>(reason);
    if (index < fallbackCounts.size()) {
      fallbackCounts[index].fetch_add(1, std::memory_order_relaxed);
    }
  }

  /// Returns the number of failed attempts for the specified fallback reason.
  uint32_t fallbackCount(PrecompiledFallbackReason reason) const {
    auto index = static_cast<size_t>(reason);
    if (index >= fallbackCounts.size()) {
      return 0;
    }
    return fallbackCounts[index].load(std::memory_order_relaxed);
  }

  /// Resets hit, miss, and fallback-reason counters to zero.
  void resetStats() {
    _hitCount.store(0, std::memory_order_relaxed);
    _missCount.store(0, std::memory_order_relaxed);
    for (auto& count : fallbackCounts) {
      count.store(0, std::memory_order_relaxed);
    }
  }

  /// Unloads all entries and resets the cache to its initial state.
  void unload() {
    vertEntries.clear();
    fragEntries.clear();
    _profileTag.clear();
    resetStats();
  }

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
  std::array<std::atomic<uint32_t>, static_cast<size_t>(PrecompiledFallbackReason::Count)>
      fallbackCounts = {};
};

}  // namespace tgfx
