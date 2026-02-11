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

#include "MtlRenderPass.h"
#include "MtlCommandEncoder.h"
#include "MtlTexture.h"
#include "MtlBuffer.h"
#include "MtlSampler.h"
#include "MtlRenderPipeline.h"
#include "MtlDefines.h"
#include "MtlShaderModule.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<MtlRenderPass> MtlRenderPass::Make(MtlCommandEncoder* encoder, 
                                                  const RenderPassDescriptor& descriptor) {
  if (!encoder) {
    return nullptr;
  }
  
  auto pass = std::shared_ptr<MtlRenderPass>(new MtlRenderPass(encoder, descriptor));
  if (!pass->renderEncoder) {
    return nullptr;
  }
  return pass;
}

MtlRenderPass::MtlRenderPass(MtlCommandEncoder* encoder, const RenderPassDescriptor& desc)
    : RenderPass(desc), commandEncoder(encoder), passDescriptor(desc) {
  // Create Metal render pass descriptor
  MTLRenderPassDescriptor* mtlRenderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
  
  // Configure color attachments
  for (size_t i = 0; i < passDescriptor.colorAttachments.size(); i++) {
    const auto& colorAttachment = passDescriptor.colorAttachments[i];
    if (!colorAttachment.texture) {
      continue;
    }
    
    auto colorTexture = std::static_pointer_cast<MtlTexture>(colorAttachment.texture);
    
    mtlRenderPassDescriptor.colorAttachments[i].texture = colorTexture->mtlTexture();
    mtlRenderPassDescriptor.colorAttachments[i].loadAction = 
        (colorAttachment.loadAction == LoadAction::Clear) ? MTLLoadActionClear :
        (colorAttachment.loadAction == LoadAction::Load) ? MTLLoadActionLoad : MTLLoadActionDontCare;
    mtlRenderPassDescriptor.colorAttachments[i].storeAction = 
        (colorAttachment.storeAction == StoreAction::Store) ? MTLStoreActionStore :
        (colorAttachment.storeAction == StoreAction::DontCare) ? MTLStoreActionDontCare : MTLStoreActionMultisampleResolve;
    
    if (colorAttachment.loadAction == LoadAction::Clear) {
      auto clearColor = colorAttachment.clearValue;
      mtlRenderPassDescriptor.colorAttachments[i].clearColor = 
          MTLClearColorMake(clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha);
    }
    
    // Configure resolve texture if present (for MSAA)
    if (colorAttachment.resolveTexture) {
      auto resolveTexture = std::static_pointer_cast<MtlTexture>(colorAttachment.resolveTexture);
      mtlRenderPassDescriptor.colorAttachments[i].resolveTexture = resolveTexture->mtlTexture();
      // Use StoreAndMultisampleResolve to keep MSAA texture content for future Load operations
      mtlRenderPassDescriptor.colorAttachments[i].storeAction = MTLStoreActionStoreAndMultisampleResolve;
    }
  }
  
  // Configure depth-stencil attachment
  if (passDescriptor.depthStencilAttachment.texture) {
    auto depthStencilTexture = std::static_pointer_cast<MtlTexture>(passDescriptor.depthStencilAttachment.texture);
    
    mtlRenderPassDescriptor.depthAttachment.texture = depthStencilTexture->mtlTexture();
    mtlRenderPassDescriptor.depthAttachment.loadAction = 
        (passDescriptor.depthStencilAttachment.loadAction == LoadAction::Clear) ? MTLLoadActionClear :
        (passDescriptor.depthStencilAttachment.loadAction == LoadAction::Load) ? MTLLoadActionLoad : MTLLoadActionDontCare;
    mtlRenderPassDescriptor.depthAttachment.storeAction = 
        (passDescriptor.depthStencilAttachment.storeAction == StoreAction::Store) ? MTLStoreActionStore : MTLStoreActionDontCare;
    
    mtlRenderPassDescriptor.stencilAttachment.texture = depthStencilTexture->mtlTexture();
    mtlRenderPassDescriptor.stencilAttachment.loadAction = 
        (passDescriptor.depthStencilAttachment.loadAction == LoadAction::Clear) ? MTLLoadActionClear :
        (passDescriptor.depthStencilAttachment.loadAction == LoadAction::Load) ? MTLLoadActionLoad : MTLLoadActionDontCare;
    mtlRenderPassDescriptor.stencilAttachment.storeAction = 
        (passDescriptor.depthStencilAttachment.storeAction == StoreAction::Store) ? MTLStoreActionStore : MTLStoreActionDontCare;
    
    if (passDescriptor.depthStencilAttachment.loadAction == LoadAction::Clear) {
      mtlRenderPassDescriptor.depthAttachment.clearDepth = passDescriptor.depthStencilAttachment.depthClearValue;
      mtlRenderPassDescriptor.stencilAttachment.clearStencil = passDescriptor.depthStencilAttachment.stencilClearValue;
    }
  }
  
  // Create render command encoder
  renderEncoder = [[commandEncoder->mtlCommandBuffer() renderCommandEncoderWithDescriptor:mtlRenderPassDescriptor] retain];
  
  // Set default viewport based on the first color attachment
  if (renderEncoder && !passDescriptor.colorAttachments.empty() && 
      passDescriptor.colorAttachments[0].texture) {
    auto texture = passDescriptor.colorAttachments[0].texture;
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

void MtlRenderPass::onEnd() {
  if (renderEncoder) {
    [renderEncoder endEncoding];
    [renderEncoder release];
    renderEncoder = nil;
  }
}

GPU* MtlRenderPass::gpu() const {
  return commandEncoder->gpu();
}

void MtlRenderPass::setViewport(int x, int y, int width, int height) {
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

void MtlRenderPass::setScissorRect(int x, int y, int width, int height) {
  if (!renderEncoder) return;
  
  // Clamp to render target bounds - Metal requires non-negative values
  // and scissor rect must be within render target bounds
  int renderWidth = 0;
  int renderHeight = 0;
  if (!passDescriptor.colorAttachments.empty() && passDescriptor.colorAttachments[0].texture) {
    renderWidth = passDescriptor.colorAttachments[0].texture->width();
    renderHeight = passDescriptor.colorAttachments[0].texture->height();
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
  
  // Ensure non-negative dimensions
  if (width <= 0 || height <= 0) {
    // Set to empty scissor (nothing will be drawn)
    width = 0;
    height = 0;
    x = 0;
    y = 0;
  }
  
  MTLScissorRect scissorRect;
  scissorRect.x = static_cast<NSUInteger>(x);
  scissorRect.y = static_cast<NSUInteger>(y);
  scissorRect.width = static_cast<NSUInteger>(width);
  scissorRect.height = static_cast<NSUInteger>(height);
  
  [renderEncoder setScissorRect:scissorRect];
}

void MtlRenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (!renderEncoder || !pipeline) return;
  
  currentPipeline = std::static_pointer_cast<MtlRenderPipeline>(pipeline);
  [renderEncoder setRenderPipelineState:currentPipeline->mtlRenderPipelineState()];
  
  // Set depth stencil state if available
  if (currentPipeline->mtlDepthStencilState()) {
    [renderEncoder setDepthStencilState:currentPipeline->mtlDepthStencilState()];
  }

  // Set cull mode and front face winding
  [renderEncoder setCullMode:currentPipeline->cullMode];
  [renderEncoder setFrontFacingWinding:currentPipeline->frontFace];
}

void MtlRenderPass::setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer, size_t offset) {
  if (!renderEncoder || !buffer) {
    return;
  }
  
  DEBUG_ASSERT(slot <= kVertexBufferIndexStart);
  auto mtlBufferIndex = kVertexBufferIndexStart - slot;
  auto mtlBuffer = std::static_pointer_cast<MtlBuffer>(buffer);
  [renderEncoder setVertexBuffer:mtlBuffer->mtlBuffer()
                          offset:offset
                         atIndex:mtlBufferIndex];
}

void MtlRenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  // Metal doesn't have a separate setIndexBuffer method
  // Index buffer is set during drawIndexed call
  indexBuffer = std::static_pointer_cast<MtlBuffer>(buffer);
  indexFormat = format;
}

void MtlRenderPass::setTexture(unsigned binding, 
                              std::shared_ptr<Texture> texture,
                              std::shared_ptr<Sampler> sampler) {
  if (!renderEncoder || !texture) {
    return;
  }
  
  // Remap logical binding to actual Metal texture index via the pipeline's mapping table.
  unsigned textureIndex = currentPipeline ? currentPipeline->getTextureIndex(binding) : binding;
  
  auto mtlTexture = std::static_pointer_cast<MtlTexture>(texture);
  [renderEncoder setFragmentTexture:mtlTexture->mtlTexture() atIndex:textureIndex];
  
  if (sampler) {
    auto mtlSampler = std::static_pointer_cast<MtlSampler>(sampler);
    [renderEncoder setFragmentSamplerState:mtlSampler->mtlSamplerState() atIndex:textureIndex];
  }
}

void MtlRenderPass::setUniformBuffer(unsigned binding,
                                   std::shared_ptr<GPUBuffer> buffer, 
                                   size_t offset, 
                                   size_t size) {
  if (!renderEncoder || !buffer) {
    return;
  }
  DEBUG_ASSERT(binding < kVertexBufferIndexStart);
  (void)size;  // Metal doesn't need explicit size
  
  auto mtlBuffer = std::static_pointer_cast<MtlBuffer>(buffer);
  
  // Uniform buffers use low Metal buffer indices (0, 1, ...) directly.
  [renderEncoder setVertexBuffer:mtlBuffer->mtlBuffer() 
                          offset:offset 
                         atIndex:binding];
  [renderEncoder setFragmentBuffer:mtlBuffer->mtlBuffer() 
                            offset:offset 
                           atIndex:binding];
}

void MtlRenderPass::setStencilReference(uint32_t reference) {
  if (!renderEncoder) {
    return;
  }
  [renderEncoder setStencilReferenceValue:reference];
}

void MtlRenderPass::draw(PrimitiveType primitiveType, uint32_t vertexCount, uint32_t instanceCount,
                        uint32_t firstVertex, uint32_t firstInstance) {
  if (!renderEncoder) {
    return;
  }
  
  MTLPrimitiveType mtlPrimitiveType = MtlDefines::ToMTLPrimitiveType(primitiveType);
  [renderEncoder drawPrimitives:mtlPrimitiveType
                    vertexStart:firstVertex
                    vertexCount:vertexCount
                  instanceCount:instanceCount
                   baseInstance:firstInstance];
}

void MtlRenderPass::drawIndexed(PrimitiveType primitiveType, 
                               uint32_t indexCount, 
                               uint32_t instanceCount,
                               uint32_t firstIndex,
                               int32_t baseVertex,
                               uint32_t firstInstance) {
  if (!renderEncoder || !indexBuffer) {
    return;
  }
  
  MTLPrimitiveType mtlPrimitiveType = MtlDefines::ToMTLPrimitiveType(primitiveType);
  
  // Convert IndexFormat to Metal types
  MTLIndexType indexType = (indexFormat == IndexFormat::UInt32) ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
  size_t indexSize = (indexFormat == IndexFormat::UInt32) ? sizeof(uint32_t) : sizeof(uint16_t);
  
  [renderEncoder drawIndexedPrimitives:mtlPrimitiveType
                            indexCount:indexCount
                             indexType:indexType
                           indexBuffer:indexBuffer->mtlBuffer()
                     indexBufferOffset:(firstIndex * indexSize)
                         instanceCount:instanceCount
                            baseVertex:baseVertex
                          baseInstance:firstInstance];
}

}  // namespace tgfx
