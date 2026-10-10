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
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

class Context;

/**
 * Counters of the shader cache. They only grow (until resetStats()), so clear() and eviction do
 * not rewrite history. Unlike ProgramCacheStats they are never paused: they describe how often the
 * GPU was asked to compile, which is the same question for reference renders and production draws.
 */
struct ShaderCacheStats {
  uint64_t moduleRequests = 0;
  uint64_t moduleHits = 0;
  // Modules the GPU created successfully, and attempts it rejected (failures are never cached).
  uint64_t moduleCreations = 0;
  uint64_t moduleCreationFailures = 0;
  uint64_t moduleEvictions = 0;
  // Wall time spent inside GPU::createShaderModule() for the creations and failures above.
  int64_t moduleCreationMicros = 0;
};

/**
 * The content key of a shader module: a digest of every ShaderModuleDescriptor field a backend
 * reads when it builds the module (format, stage, code, binary data and uniform slots), plus the
 * encoded length. Two descriptors with the same key produce interchangeable modules on every
 * backend. The key holds no pointers or process-local values, so it stays meaningful across runs
 * and can later key a persistent cache.
 */
struct ShaderModuleKey {
  std::array<uint8_t, 16> digest = {};
  uint64_t encodedSize = 0;

  bool operator==(const ShaderModuleKey& other) const {
    return encodedSize == other.encodedSize && digest == other.digest;
  }
};

struct ShaderModuleKeyHasher {
  size_t operator()(const ShaderModuleKey& key) const {
    uint64_t value = 0;
    std::memcpy(&value, key.digest.data(), sizeof(value));
    return static_cast<size_t>(value ^ key.encodedSize);
  }
};

/**
 * ShaderCache reuses the GPU objects that are expensive to create, independently of the backend
 * and of where the shader came from (the precompiled bundle or the runtime program builder). It
 * sits below the program cache in GlobalCache: a program cache miss still builds a descriptor, and
 * this cache decides whether the GPU really has to compile it.
 *
 * Shader modules are keyed by their content (see ShaderModuleKey), so the cache never needs
 * invalidating when a bundle is unloaded or reloaded: identical input always gives an identical
 * module. Entries are strong references kept in an LRU, so a program evicted from the program
 * cache does not cost a recompilation when it comes back. The cache belongs to the Context and is
 * destroyed with it, which is also what happens when the GPU context is lost.
 *
 * TGFX_SHADER_CACHE_DISABLE=L1 (or =all) bypasses the module cache, for A/B comparisons.
 */
class ShaderCache {
 public:
  explicit ShaderCache(Context* context);

  /**
   * Returns a module for the descriptor: a cached one when an identical descriptor was compiled
   * before, otherwise a new one from GPU::createShaderModule(). Returns nullptr when the GPU fails
   * to create it; failures are not cached, so the next request tries again.
   */
  std::shared_ptr<ShaderModule> findOrCreateModule(const ShaderModuleDescriptor& descriptor);

  /**
   * Drops every cached object. Objects still referenced by programs stay alive until those
   * programs are released. Intended for tests that need a really cold start.
   */
  void clear();

  size_t moduleCount() const {
    return moduleMap.size();
  }

  const ShaderCacheStats& stats() const {
    return _stats;
  }

  void resetStats() {
    _stats = {};
  }

  /**
   * The most modules kept alive. The program cache holds at most 256 programs, so live programs
   * alone can reference up to 512 modules; the module cache is larger than that so modules of
   * recently evicted programs survive too.
   */
  static constexpr size_t MaxModuleCount = 1024;

 private:
  struct ModuleEntry {
    std::shared_ptr<ShaderModule> module = nullptr;
    std::list<ShaderModuleKey>::iterator lruPosition = {};
#ifdef DEBUG
    // The full encoding, compared on every hit in debug builds so a digest collision or an
    // encoding that misses a field cannot silently hand out the wrong module.
    std::string encoded = {};
#endif
  };

  Context* context = nullptr;
  bool moduleCacheEnabled = true;
  std::list<ShaderModuleKey> moduleLRU = {};
  std::unordered_map<ShaderModuleKey, ModuleEntry, ShaderModuleKeyHasher> moduleMap = {};
  ShaderCacheStats _stats = {};

  std::shared_ptr<ShaderModule> createModule(const ShaderModuleDescriptor& descriptor);
};

/**
 * Appends every field of the descriptor that a backend reads to build a module, in a fixed order.
 * Exposed for tests that check the encoding distinguishes every field.
 */
void EncodeShaderModuleDescriptor(const ShaderModuleDescriptor& descriptor, std::string* out);

ShaderModuleKey MakeShaderModuleKey(const std::string& encoded);

}  // namespace tgfx
