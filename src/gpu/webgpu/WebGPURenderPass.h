/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

  void setVertexBuffer(std::shared_ptr<GPUBuffer> buffer, size_t offset) override;

  void setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) override;

  void setStencilReference(uint32_t reference) override;

  void draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount) override;

  void drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount) override;

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
