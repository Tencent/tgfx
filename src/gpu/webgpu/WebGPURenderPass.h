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
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {

class WebGPUGPU;
class WebGPURenderPipeline;

class WebGPURenderPass : public RenderPass {
 public:
  static std::shared_ptr<WebGPURenderPass> Make(WebGPUGPU* gpu, WGPUCommandEncoder encoder,
                                                const RenderPassDescriptor& descriptor);

  GPU* gpu() const override;

  void setViewport(int x, int y, int width, int height) override;

  void setScissorRect(int x, int y, int width, int height) override;

  void setPipeline(std::shared_ptr<RenderPipeline> pipeline) override;

  void setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer, size_t offset,
                        size_t size) override;

  void setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                  std::shared_ptr<Sampler> sampler) override;

  void setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                       size_t offset = 0) override;

  void setIndexBuffer(std::shared_ptr<GPUBuffer> buffer,
                      IndexFormat format = IndexFormat::UInt16) override;

  void setStencilReference(uint32_t reference) override;

  void draw(PrimitiveType primitiveType, uint32_t vertexCount, uint32_t instanceCount = 1,
            uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;

  void drawIndexed(PrimitiveType primitiveType, uint32_t indexCount, uint32_t instanceCount = 1,
                   uint32_t firstIndex = 0, int32_t baseVertex = 0,
                   uint32_t firstInstance = 0) override;

 protected:
  void onEnd() override;

 private:
  WebGPURenderPass(WebGPUGPU* gpu, WGPURenderPassEncoder encoder,
                   const RenderPassDescriptor& descriptor);

  void updateBindGroup();

  WebGPUGPU* _gpu = nullptr;
  WGPURenderPassEncoder passEncoder = nullptr;
  std::shared_ptr<WebGPURenderPipeline> currentPipeline = nullptr;

  struct UniformBinding {
    std::shared_ptr<GPUBuffer> buffer = nullptr;
    size_t offset = 0;
    size_t size = 0;
  };
  std::unordered_map<unsigned, UniformBinding> pendingUniforms = {};

  struct TextureBinding {
    std::shared_ptr<Texture> texture = nullptr;
    std::shared_ptr<Sampler> sampler = nullptr;
  };
  std::unordered_map<unsigned, TextureBinding> pendingTextures = {};

  bool bindGroupDirty = false;
  WGPURenderPipeline lastBoundPipeline = nullptr;
};

}  // namespace tgfx
