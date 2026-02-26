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

#include <webgpu/webgpu_cpp.h>
#include <map>
#include "WebGPUBuffer.h"
#include "WebGPURenderPipeline.h"
#include "WebGPUSampler.h"
#include "WebGPUTexture.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
class WebGPUGPU;

class WebGPURenderPass : public RenderPass {
 public:
  WebGPURenderPass(WebGPUGPU* gpu, RenderPassDescriptor descriptor,
                   wgpu::RenderPassEncoder passEncoder);

  GPU* gpu() const override;

  void setViewport(int x, int y, int width, int height) override;

  void setScissorRect(int x, int y, int width, int height) override;

  void setPipeline(std::shared_ptr<RenderPipeline> pipeline) override;

  void setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer, size_t offset,
                        size_t size) override;

  void setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                  std::shared_ptr<Sampler> sampler) override;

  void setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer, size_t offset = 0) override;

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
  WebGPUGPU* _gpu = nullptr;
  wgpu::RenderPassEncoder _passEncoder = nullptr;
  std::shared_ptr<WebGPURenderPipeline> renderPipeline = nullptr;
  std::map<uint32_t, wgpu::BindGroupEntry> pendingBindGroupEntries = {};
  bool bindGroupDirty = false;

  void flushBindGroup();
};
}  // namespace tgfx
