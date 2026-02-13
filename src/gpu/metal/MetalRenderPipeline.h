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

#include <Metal/Metal.h>
#include <unordered_map>
#include "tgfx/gpu/RenderPipeline.h"
#include "MetalResource.h"

namespace tgfx {

class MetalGPU;

/**
 * Metal render pipeline implementation.
 */
class MetalRenderPipeline : public RenderPipeline, public MetalResource {
 public:
  static std::shared_ptr<MetalRenderPipeline> Make(MetalGPU* gpu, const RenderPipelineDescriptor& descriptor);

  /**
   * Returns the Metal render pipeline state.
   */
  id<MTLRenderPipelineState> metalRenderPipelineState() const {
    return pipelineState;
  }

  /**
   * Returns the Metal depth stencil state.
   */
  id<MTLDepthStencilState> metalDepthStencilState() const {
    return depthStencilState;
  }

  /**
   * Returns the actual Metal texture index for the given logical binding point.
   * Returns the binding value unchanged if no mapping is found (for custom shaders that set
   * textures directly without a BindingLayout).
   */
  unsigned getTextureIndex(unsigned binding) const;

 protected:
  void onRelease(MetalGPU* gpu) override;

 private:
  MetalRenderPipeline(MetalGPU* gpu, const RenderPipelineDescriptor& descriptor);
  ~MetalRenderPipeline() override = default;

  bool createPipelineState(MetalGPU* gpu, const RenderPipelineDescriptor& descriptor);
  bool createDepthStencilState(id<MTLDevice> device, const RenderPipelineDescriptor& descriptor);

  id<MTLRenderPipelineState> pipelineState = nil;
  id<MTLDepthStencilState> depthStencilState = nil;
  id<MTLLibrary> sampleMaskLibrary = nil;
  std::unordered_map<unsigned, unsigned> textureUnits = {};
  MTLCullMode cullMode = MTLCullModeNone;
  MTLWinding frontFace = MTLWindingCounterClockwise;
  
  friend class MetalGPU;
  friend class MetalRenderPass;
};

}  // namespace tgfx