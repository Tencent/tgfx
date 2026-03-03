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

#include "MetalRenderPass.h"
#include "MetalBuffer.h"
#include "MetalCommandEncoder.h"
#include "MetalDefines.h"
#include "MetalRenderPipeline.h"
#include "MetalSampler.h"
#include "MetalShaderModule.h"
#include "MetalTexture.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<MetalRenderPass> MetalRenderPass::Make(MetalCommandEncoder* encoder,
                                                       const RenderPassDescriptor& descriptor) {
  if (!encoder) {
    return nullptr;
  }

  auto pass = std::shared_ptr<MetalRenderPass>(new MetalRenderPass(encoder, descriptor));
  if (!pass->renderEncoder) {
    return nullptr;
  }
  return pass;
}

MetalRenderPass::MetalRenderPass(MetalCommandEncoder* encoder, const RenderPassDescriptor& desc)
    : RenderPass(desc), commandEncoder(encoder) {
  // Create Metal render pass descriptor
  MTLRenderPassDescriptor* metalRenderPassDescriptor =
      [MTLRenderPassDescriptor renderPassDescriptor];

  // Configure color attachments
  for (size_t i = 0; i < descriptor.colorAttachments.size(); i++) {
    const auto& colorAttachment = descriptor.colorAttachments[i];
    if (!colorAttachment.texture) {
      continue;
    }

    auto colorTexture = std::static_pointer_cast<MetalTexture>(colorAttachment.texture);

    metalRenderPassDescriptor.colorAttachments[i].texture = colorTexture->metalTexture();
    metalRenderPassDescriptor.colorAttachments[i].loadAction =
        MetalDefines::ToMTLLoadAction(colorAttachment.loadAction);
    metalRenderPassDescriptor.colorAttachments[i].storeAction =
        MetalDefines::ToMTLStoreAction(colorAttachment.storeAction);

    if (colorAttachment.loadAction == LoadAction::Clear) {
      auto clearColor = colorAttachment.clearValue;
      metalRenderPassDescriptor.colorAttachments[i].clearColor =
          MTLClearColorMake(clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha);
    }

    // Configure resolve texture if present (for MSAA)
    if (colorAttachment.resolveTexture) {
      auto resolveTexture = std::static_pointer_cast<MetalTexture>(colorAttachment.resolveTexture);
      metalRenderPassDescriptor.colorAttachments[i].resolveTexture = resolveTexture->metalTexture();
      // Use StoreAndMultisampleResolve to keep MSAA texture content for future Load operations
      metalRenderPassDescriptor.colorAttachments[i].storeAction =
          MTLStoreActionStoreAndMultisampleResolve;
    }
  }

  // Configure depth-stencil attachment
  if (descriptor.depthStencilAttachment.texture) {
    auto depthStencilTexture =
        std::static_pointer_cast<MetalTexture>(descriptor.depthStencilAttachment.texture);

    metalRenderPassDescriptor.depthAttachment.texture = depthStencilTexture->metalTexture();
    metalRenderPassDescriptor.depthAttachment.loadAction =
        MetalDefines::ToMTLLoadAction(descriptor.depthStencilAttachment.loadAction);
    metalRenderPassDescriptor.depthAttachment.storeAction =
        MetalDefines::ToMTLStoreAction(descriptor.depthStencilAttachment.storeAction);

    metalRenderPassDescriptor.stencilAttachment.texture = depthStencilTexture->metalTexture();
    metalRenderPassDescriptor.stencilAttachment.loadAction =
        MetalDefines::ToMTLLoadAction(descriptor.depthStencilAttachment.loadAction);
    metalRenderPassDescriptor.stencilAttachment.storeAction =
        MetalDefines::ToMTLStoreAction(descriptor.depthStencilAttachment.storeAction);

    if (descriptor.depthStencilAttachment.loadAction == LoadAction::Clear) {
      metalRenderPassDescriptor.depthAttachment.clearDepth =
          descriptor.depthStencilAttachment.depthClearValue;
      metalRenderPassDescriptor.stencilAttachment.clearStencil =
          descriptor.depthStencilAttachment.stencilClearValue;
    }
  }

  // Create render command encoder
  renderEncoder = [[commandEncoder->metalCommandBuffer()
      renderCommandEncoderWithDescriptor:metalRenderPassDescriptor] retain];

  // Set default viewport based on the first color attachment
  if (renderEncoder && !descriptor.colorAttachments.empty() &&
      descriptor.colorAttachments[0].texture) {
    auto texture = descriptor.colorAttachments[0].texture;
    MTLViewport viewport;
    viewport.originX = 0.0;
    viewport.originY = 0.0;
    viewport.width = static_cast<double>(texture->width());
    viewport.height = static_cast<double>(texture->height());
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    [renderEncoder setViewport:viewport];
  }
}

void MetalRenderPass::onEnd() {
  if (renderEncoder) {
    [renderEncoder endEncoding];
    [renderEncoder release];
    renderEncoder = nil;
  }
}

MetalRenderPass::~MetalRenderPass() {
  if (renderEncoder) {
    [renderEncoder endEncoding];
    [renderEncoder release];
    renderEncoder = nil;
  }
}

GPU* MetalRenderPass::gpu() const {
  return commandEncoder->gpu();
}

void MetalRenderPass::setViewport(int x, int y, int width, int height) {
  if (!renderEncoder) return;

  MTLViewport viewport;
  viewport.originX = static_cast<double>(x);
  viewport.originY = static_cast<double>(y);
  viewport.width = static_cast<double>(width);
  viewport.height = static_cast<double>(height);
  viewport.znear = 0.0;
  viewport.zfar = 1.0;

  [renderEncoder setViewport:viewport];
}

void MetalRenderPass::setScissorRect(int x, int y, int width, int height) {
  if (!renderEncoder) return;

  // Clamp to render target bounds - Metal requires non-negative values
  // and scissor rect must be within render target bounds
  int renderWidth = 0;
  int renderHeight = 0;
  if (!descriptor.colorAttachments.empty() && descriptor.colorAttachments[0].texture) {
    renderWidth = descriptor.colorAttachments[0].texture->width();
    renderHeight = descriptor.colorAttachments[0].texture->height();
  }

  // Clamp negative coordinates
  if (x < 0) {
    width += x;  // Reduce width by the amount x is negative
    x = 0;
  }
  if (y < 0) {
    height += y;  // Reduce height by the amount y is negative
    y = 0;
  }

  // Clamp to render target bounds
  if (x + width > renderWidth) {
    width = renderWidth - x;
  }
  if (y + height > renderHeight) {
    height = renderHeight - y;
  }

  // Metal does not allow scissor rect with zero width or height.
  if (width <= 0 || height <= 0) {
    return;
  }

  MTLScissorRect scissorRect;
  scissorRect.x = static_cast<NSUInteger>(x);
  scissorRect.y = static_cast<NSUInteger>(y);
  scissorRect.width = static_cast<NSUInteger>(width);
  scissorRect.height = static_cast<NSUInteger>(height);

  [renderEncoder setScissorRect:scissorRect];
}

void MetalRenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (!renderEncoder || !pipeline) return;

  currentPipeline = std::static_pointer_cast<MetalRenderPipeline>(pipeline);
  [renderEncoder setRenderPipelineState:currentPipeline->metalRenderPipelineState()];

  // Set depth stencil state if available
  if (currentPipeline->metalDepthStencilState()) {
    [renderEncoder setDepthStencilState:currentPipeline->metalDepthStencilState()];
  }

  // Set cull mode and front face winding
  [renderEncoder setCullMode:currentPipeline->cullMode];
  [renderEncoder setFrontFacingWinding:currentPipeline->frontFace];
}

void MetalRenderPass::setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                                      size_t offset) {
  if (!renderEncoder || !buffer) {
    return;
  }

  DEBUG_ASSERT(slot < VertexBufferIndexStart);
  auto metalBufferIndex = VertexBufferIndexStart - slot;
  auto metalBuffer = std::static_pointer_cast<MetalBuffer>(buffer);
  [renderEncoder setVertexBuffer:metalBuffer->metalBuffer() offset:offset atIndex:metalBufferIndex];
}

void MetalRenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  // Metal doesn't have a separate setIndexBuffer method
  // Index buffer is set during drawIndexed call
  indexBuffer = std::static_pointer_cast<MetalBuffer>(buffer);
  indexFormat = format;
}

void MetalRenderPass::setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                                 std::shared_ptr<Sampler> sampler) {
  if (!renderEncoder || !texture) {
    return;
  }

  // Remap logical binding to actual Metal texture index via the pipeline's mapping table.
  unsigned textureIndex = currentPipeline ? currentPipeline->getTextureIndex(binding) : binding;

  auto metalTexture = std::static_pointer_cast<MetalTexture>(texture);
  [renderEncoder setFragmentTexture:metalTexture->metalTexture() atIndex:textureIndex];

  if (sampler) {
    auto metalSampler = std::static_pointer_cast<MetalSampler>(sampler);
    [renderEncoder setFragmentSamplerState:metalSampler->metalSamplerState() atIndex:textureIndex];
  }
}

void MetalRenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset, size_t size) {
  if (!renderEncoder || !buffer) {
    return;
  }
  DEBUG_ASSERT(binding < VertexBufferIndexStart);
  (void)size;  // Metal doesn't need explicit size

  auto metalBuffer = std::static_pointer_cast<MetalBuffer>(buffer);

  // Uniform buffers use low Metal buffer indices (0, 1, ...) directly.
  [renderEncoder setVertexBuffer:metalBuffer->metalBuffer() offset:offset atIndex:binding];
  [renderEncoder setFragmentBuffer:metalBuffer->metalBuffer() offset:offset atIndex:binding];
}

void MetalRenderPass::setStencilReference(uint32_t reference) {
  if (!renderEncoder) {
    return;
  }
  [renderEncoder setStencilReferenceValue:reference];
}

void MetalRenderPass::draw(PrimitiveType primitiveType, uint32_t vertexCount,
                           uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
  if (!renderEncoder) {
    return;
  }

  MTLPrimitiveType metalPrimitiveType = MetalDefines::ToMTLPrimitiveType(primitiveType);
  [renderEncoder drawPrimitives:metalPrimitiveType
                    vertexStart:firstVertex
                    vertexCount:vertexCount
                  instanceCount:instanceCount
                   baseInstance:firstInstance];
}

void MetalRenderPass::drawIndexed(PrimitiveType primitiveType, uint32_t indexCount,
                                  uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                                  uint32_t firstInstance) {
  if (!renderEncoder || !indexBuffer) {
    return;
  }

  MTLPrimitiveType metalPrimitiveType = MetalDefines::ToMTLPrimitiveType(primitiveType);

  // Convert IndexFormat to Metal types
  MTLIndexType indexType =
      (indexFormat == IndexFormat::UInt32) ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
  size_t indexSize = (indexFormat == IndexFormat::UInt32) ? sizeof(uint32_t) : sizeof(uint16_t);

  [renderEncoder drawIndexedPrimitives:metalPrimitiveType
                            indexCount:indexCount
                             indexType:indexType
                           indexBuffer:indexBuffer->metalBuffer()
                     indexBufferOffset:(firstIndex * indexSize)
                         instanceCount:instanceCount
                            baseVertex:baseVertex
                          baseInstance:firstInstance];
}

}  // namespace tgfx
