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
#include "MetalShaderModule.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {

class MetalCommandEncoder;
class MetalRenderPipeline;

/**
 * Metal render pass implementation.
 */
class MetalRenderPass : public RenderPass {
 public:
  static std::shared_ptr<MetalRenderPass> Make(MetalCommandEncoder* encoder,
                                               const RenderPassDescriptor& descriptor);

  ~MetalRenderPass() override;

  /**
   * Returns the Metal render command encoder.
   */
  id<MTLRenderCommandEncoder> metalRenderCommandEncoder() const {
    return renderEncoder;
  }

  // RenderPass interface implementation
  GPU* gpu() const override;
  void setViewport(int x, int y, int width, int height) override;
  void setScissorRect(int x, int y, int width, int height) override;
  void setPipeline(std::shared_ptr<RenderPipeline> pipeline) override;
  void setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                       size_t offset = 0) override;
  void setIndexBuffer(std::shared_ptr<GPUBuffer> buffer,
                      IndexFormat format = IndexFormat::UInt16) override;
  void setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                  std::shared_ptr<Sampler> sampler) override;
  void setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer, size_t offset,
                        size_t size) override;
  void setStencilReference(uint32_t reference) override;

  void draw(PrimitiveType primitiveType, uint32_t vertexCount, uint32_t instanceCount = 1,
            uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
  void drawIndexed(PrimitiveType primitiveType, uint32_t indexCount, uint32_t instanceCount = 1,
                   uint32_t firstIndex = 0, int32_t baseVertex = 0,
                   uint32_t firstInstance = 0) override;

 protected:
  void onEnd() override;

 private:
  MetalRenderPass(MetalCommandEncoder* encoder, const RenderPassDescriptor& descriptor);

  MetalCommandEncoder* commandEncoder = nullptr;
  id<MTLRenderCommandEncoder> renderEncoder = nil;
  std::shared_ptr<MetalRenderPipeline> currentPipeline = nullptr;

  // Set to true when a resource-binding call arrives before setPipeline() and cannot be routed to
  // a physical shader-stage slot. Metal is the only backend that translates public logical
  // bindings to physical slots at the setter-call site rather than at draw/flush time, so it
  // cannot silently defer a bad binding — the subsequent draw would otherwise use stale or empty
  // slots. Cleared by setPipeline() so a well-formed run after a bad prefix still works.
  bool hasInvalidBindings = false;

  // Cached scissor rect to avoid redundant Metal API calls.
  int lastScissorX = -1;
  int lastScissorY = -1;
  int lastScissorWidth = -1;
  int lastScissorHeight = -1;

  // Cached uniform buffer state is indexed by the physical Metal slot for each stage. Public
  // bindings are pipeline-scoped logical IDs and may resolve to different vertex/fragment slots.
  static constexpr int MaxUniformBindings = VertexBufferIndexStart;
  GPUBuffer* lastVertexUniformBuffers[MaxUniformBindings] = {};
  size_t lastVertexUniformOffsets[MaxUniformBindings] = {};
  GPUBuffer* lastFragmentUniformBuffers[MaxUniformBindings] = {};
  size_t lastFragmentUniformOffsets[MaxUniformBindings] = {};

  // Cached texture/sampler state to avoid redundant Metal API calls.
  static constexpr int MaxTextureBindings = 16;
  Texture* lastTextures[MaxTextureBindings] = {};
  Sampler* lastSamplers[MaxTextureBindings] = {};

  // Cached vertex buffer state to avoid redundant Metal API calls.
  static constexpr int MaxVertexBufferSlots = 4;
  GPUBuffer* lastVertexBuffers[MaxVertexBufferSlots] = {};
  size_t lastVertexOffsets[MaxVertexBufferSlots] = {};

  // Cached pipeline sub-states to avoid redundant Metal API calls.
  id<MTLDepthStencilState> lastDepthStencilState = nil;
  MTLCullMode lastCullMode = MTLCullModeNone;
  MTLWinding lastFrontFace = MTLWindingClockwise;
  bool cullModeInitialized = false;

  // Index buffer state (Metal doesn't have separate setIndexBuffer)
  std::shared_ptr<class MetalBuffer> indexBuffer = nullptr;
  IndexFormat indexFormat = IndexFormat::UInt16;
};

}  // namespace tgfx