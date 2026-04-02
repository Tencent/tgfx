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

#include <webgpu/webgpu.h>
#include <unordered_map>
#include <vector>
#include "WebGPUResource.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class WebGPUGPU;

class WebGPURenderPipeline : public RenderPipeline, public WebGPUResource {
 public:
  static std::shared_ptr<WebGPURenderPipeline> Make(WebGPUGPU* gpu,
                                                    const RenderPipelineDescriptor& descriptor);

  WGPURenderPipeline webgpuRenderPipeline(
      WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList) const {
    return topology == WGPUPrimitiveTopology_TriangleStrip && pipelineStrip != nullptr
               ? pipelineStrip
               : pipeline;
  }

  WGPUBindGroupLayout bindGroupLayout() const {
    return _bindGroupLayout;
  }

  unsigned getTextureIndex(unsigned binding) const;

  uint32_t getUniformBlockVisibility(unsigned binding) const;

  void onRelease(WebGPUGPU* gpu) override;

 private:
  WebGPURenderPipeline(WebGPUGPU* gpu, const RenderPipelineDescriptor& descriptor);
  ~WebGPURenderPipeline() override = default;

  bool createPipelineState(WebGPUGPU* gpu, const RenderPipelineDescriptor& descriptor);

  WGPURenderPipeline pipeline = nullptr;       // TriangleList
  WGPURenderPipeline pipelineStrip = nullptr;  // TriangleStrip
  WGPUBindGroupLayout _bindGroupLayout = nullptr;
  WGPUPipelineLayout pipelineLayout = nullptr;
  std::unordered_map<unsigned, unsigned> textureUnits = {};
  std::unordered_map<unsigned, uint32_t> uniformBlockVisibility = {};
  WGPUCullMode cullMode = WGPUCullMode_None;
  WGPUFrontFace frontFace = WGPUFrontFace_CCW;

  friend class WebGPUGPU;
  friend class WebGPURenderPass;
};

}  // namespace tgfx
