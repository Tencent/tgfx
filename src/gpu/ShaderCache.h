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
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include "tgfx/gpu/RenderPipeline.h"
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

class Context;
class PipelineStore;

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

  uint64_t pipelineRequests = 0;
  uint64_t pipelineHits = 0;
  // Pipelines the GPU created successfully, and attempts it rejected (failures are never cached).
  uint64_t pipelineCreations = 0;
  uint64_t pipelineCreationFailures = 0;
  // Requests created without the cache because a module in the descriptor has no content key
  // (it did not come from this cache, e.g. with the module cache disabled).
  uint64_t pipelineUncacheable = 0;
  uint64_t pipelineEvictions = 0;
  // Wall time spent inside GPU::createRenderPipeline() for the creations and failures above.
  int64_t pipelineCreationMicros = 0;
  // L2 misses served by the pipeline store instead of the GPU (see PipelineStore).
  uint64_t pipelineStoreHits = 0;
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
 * The content key of a render pipeline: the same digest form as a module key, taken over the full
 * pipeline descriptor encoding (see EncodeRenderPipelineDescriptor). The two encodings start with
 * different version bytes, so a module key never equals a pipeline key.
 */
using PipelineKey = ShaderModuleKey;

/**
 * ShaderCache reuses the GPU objects that are expensive to create, independently of the backend
 * and of where the shader came from (the precompiled bundle or the runtime program builder). It
 * sits below the program cache in GlobalCache, which keys finished programs by what is drawn:
 * different programs can still be built from identical shaders and identical pipelines, and on a
 * program cache miss this cache decides whether the GPU really has to create them again.
 *
 * It has two layers, both keyed by content so they never need invalidating when a bundle is
 * unloaded or reloaded (identical input always gives an interchangeable object):
 *  - L1, shader modules, keyed by ShaderModuleKey.
 *  - L2, render pipelines, keyed by every field of the RenderPipelineDescriptor, with its modules
 *    encoded by their L1 content keys. A pipeline holds no per-draw data (uniforms belong to the
 *    Program, buffers and textures are bound for every draw), so programs with identical
 *    descriptors can share one. Backend drivers differ in what this saves: OpenGL links the
 *    program again for every identical descriptor, while Metal already returns a repeated
 *    pipeline state cheaply.
 *
 * Entries are strong references kept in LRUs, so a program evicted from the program cache does
 * not cost a recompilation when it comes back. Backend pipelines keep no reference to the modules
 * they were built from, so without L1 a module is released as soon as its program is built. The
 * cache is owned by the Context's PrecompiledShaderCache and destroyed with the Context, after the
 * program cache, which is also what happens when the GPU context is lost.
 *
 * TGFX_SHADER_CACHE_DISABLE bypasses layers for A/B comparisons: L1, L2, or all. Disabling L1
 * disables L2 too, since pipelines are keyed by the modules' content keys.
 */
class ShaderCache {
 public:
  explicit ShaderCache(Context* context);

  ~ShaderCache();

  /**
   * Returns a module for the descriptor: a cached one when an identical descriptor was compiled
   * before, otherwise a new one from GPU::createShaderModule(). Returns nullptr when the GPU fails
   * to create it; failures are not cached, so the next request tries again.
   */
  std::shared_ptr<ShaderModule> findOrCreateModule(const ShaderModuleDescriptor& descriptor);

  /**
   * Returns a pipeline for the descriptor: a cached one when an identical descriptor was created
   * before, otherwise a new one from GPU::createRenderPipeline(). *created is set to whether the
   * GPU was really asked to create one, so callers count creation attempts, not requests. Returns
   * nullptr when the GPU fails; failures are not cached, so the next request tries again.
   */
  std::shared_ptr<RenderPipeline> findOrCreatePipeline(const RenderPipelineDescriptor& descriptor,
                                                       bool* created);

  /**
   * Returns the content key of a module this cache handed out and still holds, or nullptr for any
   * other module.
   */
  const ShaderModuleKey* findModuleKey(const ShaderModule* module) const;

  /**
   * Drops every cached object. Objects still referenced by programs stay alive until those
   * programs are released. Intended for tests that need a really cold start.
   */
  void clear();

  size_t moduleCount() const {
    return moduleMap.size();
  }

  size_t pipelineCount() const {
    return pipelineMap.size();
  }

  /**
   * Whether each layer is in use (TGFX_SHADER_CACHE_DISABLE turns them off).
   */
  bool moduleCacheActive() const {
    return moduleCacheEnabled;
  }

  bool pipelineCacheActive() const {
    return pipelineCacheEnabled;
  }

  /**
   * Sets the store consulted on pipeline cache misses (see PipelineStore), replacing any previous
   * one; nullptr removes it. No store is set by default. clear() keeps the store.
   */
  void setPipelineStore(std::unique_ptr<PipelineStore> store);

  PipelineStore* pipelineStore() const {
    return _pipelineStore.get();
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

  /**
   * The most pipelines kept alive. A full test suite run creates about 675 distinct descriptors at
   * most, and the program cache holds at most 256 programs.
   */
  static constexpr size_t MaxPipelineCount = 1024;

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

  struct PipelineEntry {
    std::shared_ptr<RenderPipeline> pipeline = nullptr;
    std::list<ShaderModuleKey>::iterator lruPosition = {};
#ifdef DEBUG
    std::string encoded = {};
#endif
  };

  Context* context = nullptr;
  bool moduleCacheEnabled = true;
  bool pipelineCacheEnabled = true;
  std::list<ShaderModuleKey> moduleLRU = {};
  std::unordered_map<ShaderModuleKey, ModuleEntry, ShaderModuleKeyHasher> moduleMap = {};
  // Reverse lookup from a cached module object to its content key, for encoding pipelines. An
  // entry lives exactly as long as the module's L1 entry, which keeps the object (and so its
  // address) alive.
  std::unordered_map<const ShaderModule*, ShaderModuleKey> moduleKeys = {};
  // Pipeline keys are digests of the full descriptor encoding, in the same form as module keys.
  std::list<ShaderModuleKey> pipelineLRU = {};
  std::unordered_map<ShaderModuleKey, PipelineEntry, ShaderModuleKeyHasher> pipelineMap = {};
  std::unique_ptr<PipelineStore> _pipelineStore = nullptr;
  ShaderCacheStats _stats = {};

  void addPipeline(const PipelineKey& key, std::string encoded,
                   std::shared_ptr<RenderPipeline> pipeline);

  std::shared_ptr<ShaderModule> createModule(const ShaderModuleDescriptor& descriptor);
  std::shared_ptr<RenderPipeline> createPipeline(const RenderPipelineDescriptor& descriptor);
};

/**
 * Appends every field of the descriptor that a backend reads to build a module, in a fixed order.
 * Exposed for tests that check the encoding distinguishes every field.
 */
void EncodeShaderModuleDescriptor(const ShaderModuleDescriptor& descriptor, std::string* out);

ShaderModuleKey MakeShaderModuleKey(const std::string& encoded);

/**
 * Appends every field of a render pipeline descriptor, in a fixed order, to out. Shader modules
 * are encoded by the content key keyOf returns for them, so descriptors pointing at different but
 * identical module objects encode the same. Returns false when keyOf returns nullptr for a module:
 * such a descriptor cannot be compared by content. A field added to RenderPipelineDescriptor (or
 * to any type it contains) must be added here and to ShaderCacheTest's per-field check.
 */
bool EncodeRenderPipelineDescriptor(
    const RenderPipelineDescriptor& descriptor,
    const std::function<const ShaderModuleKey*(const ShaderModule*)>& keyOf, std::string* out);

}  // namespace tgfx
