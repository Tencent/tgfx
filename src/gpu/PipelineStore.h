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

#include <memory>
#include <string>
#include "gpu/ShaderCache.h"
#include "tgfx/gpu/GPUInfo.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

/**
 * The extension point for keeping render pipelines beyond the in-memory shader cache, typically
 * on disk across runs. Nothing implements it yet: ShaderCache has no store unless one is set, and
 * then behaves exactly as without this interface.
 *
 * ShaderCache calls a store from its pipeline layer (L2), so a store is never consulted while L2
 * is disabled:
 *  - findPipeline() on every L2 miss, before asking the GPU to create the pipeline;
 *  - didCreatePipeline() after every successful creation by the GPU (not after hits, and not for
 *    pipelines the store itself returned).
 *
 * The two calls fit both ways backends persist pipelines:
 *  - Per entry (OpenGL program binaries): findPipeline() loads the entry for the key and rebuilds
 *    the pipeline from it; didCreatePipeline() serializes the new pipeline under the key.
 *  - Whole archive (VkPipelineCache, ID3D12PipelineLibrary, MTLBinaryArchive): the archive is
 *    seeded when the store is created, findPipeline() returns nullptr and lets the GPU create the
 *    pipeline through the seeded archive, and didCreatePipeline() only marks the archive dirty for
 *    the store to write back later.
 * didCreatePipeline() also gives a store the descriptor of every pipeline a run really needed,
 * which is what a later warm-up (creating those pipelines ahead of their first draw) records.
 *
 * Keys come from ShaderCache and contain no pointers or process-local values: two runs that build
 * the same descriptor from the same shaders produce the same key. They do not identify the driver
 * or GPU; a persistent store must namespace its entries with MakeDriverFingerprint() and drop them
 * when it changes, because a pipeline binary from another driver is at best rejected.
 *
 * A store must reject rather than trust its own data: findPipeline() returns nullptr for anything
 * it cannot load (missing, corrupt, or refused by the driver), and the GPU then creates the
 * pipeline as if no store were set.
 */
class PipelineStore {
 public:
  virtual ~PipelineStore() = default;

  /**
   * Returns a pipeline equivalent to what GPU::createRenderPipeline(descriptor) would create, or
   * nullptr when the store has none for the key.
   */
  virtual std::shared_ptr<RenderPipeline> findPipeline(const PipelineKey& key,
                                                       const RenderPipelineDescriptor& descriptor) {
    (void)key;
    (void)descriptor;
    return nullptr;
  }

  /**
   * Called after the GPU created a pipeline for the descriptor that findPipeline() could not
   * provide.
   */
  virtual void didCreatePipeline(const PipelineKey& key, const RenderPipelineDescriptor& descriptor,
                                 const std::shared_ptr<RenderPipeline>& pipeline) {
    (void)key;
    (void)descriptor;
    (void)pipeline;
  }
};

/**
 * Identifies the backend, driver and GPU a persisted pipeline was created with: the backend
 * enum and the vendor, renderer and version strings, with a format version. Backends whose
 * driver exposes a dedicated cache identity (Vulkan's pipelineCacheUUID) will need to add it,
 * since GPUInfo does not carry it.
 */
std::string MakeDriverFingerprint(const GPUInfo& info);

}  // namespace tgfx
