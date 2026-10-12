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
#include <map>
#include <unordered_map>
#include <vector>
#include "WebGPUResource.h"
#include "gpu/VaryingShaderModule.h"
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

  WGPUBindGroupLayout bindGroupLayout(unsigned group) const {
    return group < 3 ? bindGroupLayouts[group] : nullptr;
  }

  unsigned getTextureIndex(unsigned binding) const;

  /// Returns the per-stage physical bindings for the given public logical binding, or nullptr if
  /// the pipeline does not use that binding.
  const UniformSlotMapping* getUniformSlots(unsigned binding) const;

  void onRelease(WebGPUGPU* gpu) override;

 private:
  WebGPURenderPipeline(WebGPUGPU* gpu, const RenderPipelineDescriptor& descriptor);
  ~WebGPURenderPipeline() override = default;

  bool createPipelineState(WebGPUGPU* gpu, const RenderPipelineDescriptor& descriptor);

  WGPURenderPipeline pipeline = nullptr;       // TriangleList
  WGPURenderPipeline pipelineStrip = nullptr;  // TriangleStrip
  WGPUBindGroupLayout bindGroupLayouts[3] = {};
  WGPUPipelineLayout pipelineLayout = nullptr;
  std::unordered_map<unsigned, unsigned> textureUnits = {};
  std::map<unsigned, UniformSlotMapping> uniformSlots = {};
  WGPUCullMode cullMode = WGPUCullMode_None;
  WGPUFrontFace frontFace = WGPUFrontFace_CCW;

  friend class WebGPUGPU;
  friend class WebGPURenderPass;
};

}  // namespace tgfx
