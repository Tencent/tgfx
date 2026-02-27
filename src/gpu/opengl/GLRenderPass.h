/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLInterface.h"
#include "gpu/opengl/GLRenderPipeline.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
class GLGPU;

struct PendingUniformBuffer {
  unsigned binding = 0;
  std::shared_ptr<GLBuffer> buffer = nullptr;
  size_t offset = 0;
  size_t size = 0;
};

struct PendingTexture {
  unsigned binding = 0;
  std::shared_ptr<GLTexture> texture = nullptr;
  std::shared_ptr<GLSampler> sampler = nullptr;
};

struct PendingVertexBuffer {
  unsigned slot = 0;
  std::shared_ptr<GLBuffer> buffer = nullptr;
  size_t offset = 0;
};

class GLRenderPass : public RenderPass {
 public:
  GLRenderPass(GLGPU* gpu, RenderPassDescriptor descriptor);

  GPU* gpu() const override;

  bool begin();

  void setViewport(int x, int y, int width, int height) override;

  void setScissorRect(int x, int y, int width, int height) override;

  void setPipeline(std::shared_ptr<RenderPipeline> pipeline) override;

  void setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer, size_t offset,
                        size_t size) override;

  void setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                  std::shared_ptr<Sampler> sampler) override;

  void setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer, size_t offset) override;

  void setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) override;

  void setStencilReference(uint32_t reference) override;

  void draw(PrimitiveType primitiveType, uint32_t vertexCount, uint32_t instanceCount,
            uint32_t firstVertex, uint32_t firstInstance) override;

  void drawIndexed(PrimitiveType primitiveType, uint32_t indexCount, uint32_t instanceCount,
                   uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) override;

 protected:
  void onEnd() override;

 private:
  GLGPU* _gpu = nullptr;
  std::shared_ptr<GLRenderPipeline> renderPipeline = nullptr;
  std::vector<PendingUniformBuffer> pendingUniformBuffers = {};
  std::vector<PendingTexture> pendingTextures = {};
  std::vector<PendingVertexBuffer> pendingVertexBuffers = {};
  std::shared_ptr<GLBuffer> pendingIndexBuffer = nullptr;
  IndexFormat indexFormat = IndexFormat::UInt16;
  uint32_t stencilReference = 0;

  void bindFramebuffer();
  bool flushPendingBindings();
};
}  // namespace tgfx
