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

#include "MetalCommandEncoder.h"
#include "MetalGPU.h"
#include "MetalCommandQueue.h"
#include "MetalCommandBuffer.h"
#include "MetalRenderPass.h"
#include "MetalTexture.h"
#include "MetalBuffer.h"
#include "MetalDefines.h"

namespace tgfx {

static MTLOrigin MakeMTLOrigin(const Rect& rect) {
  return MTLOriginMake(static_cast<NSUInteger>(rect.x()),
                       static_cast<NSUInteger>(rect.y()), 0);
}

static MTLSize MakeMTLSize(const Rect& rect) {
  return MTLSizeMake(static_cast<NSUInteger>(rect.width()),
                     static_cast<NSUInteger>(rect.height()), 1);
}

std::shared_ptr<MetalCommandEncoder> MetalCommandEncoder::Make(MetalGPU* gpu) {
  if (!gpu) {
    return nullptr;
  }
  
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  id<MTLCommandBuffer> commandBuffer = [queue->metalCommandQueue() commandBuffer];
  if (commandBuffer == nil) {
    return nullptr;
  }
  auto encoder = gpu->makeResource<MetalCommandEncoder>(gpu, commandBuffer);
  queue->encodePendingWait(commandBuffer);
  return encoder;
}

MetalCommandEncoder::MetalCommandEncoder(MetalGPU* gpu, id<MTLCommandBuffer> buffer) 
    : _gpu(gpu), commandBuffer([buffer retain]) {
}

void MetalCommandEncoder::onRelease(MetalGPU*) {
  if (commandBuffer != nil) {
    [commandBuffer release];
    commandBuffer = nil;
  }
}

GPU* MetalCommandEncoder::gpu() const {
  return _gpu;
}

std::shared_ptr<RenderPass> MetalCommandEncoder::onBeginRenderPass(
    const RenderPassDescriptor& descriptor) {
  return MetalRenderPass::Make(this, descriptor);
}

void MetalCommandEncoder::copyTextureToTexture(std::shared_ptr<Texture> srcTexture, 
                                           const Rect& srcRect,
                                           std::shared_ptr<Texture> dstTexture, 
                                           const Point& dstOffset) {
  if (!srcTexture || !dstTexture) {
    return;
  }
  
  auto metalSrcTexture = std::static_pointer_cast<MetalTexture>(srcTexture);
  auto metalDstTexture = std::static_pointer_cast<MetalTexture>(dstTexture);
  
  id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
  if (blitEncoder == nil) {
    return;
  }
  
  auto sourceOrigin = MakeMTLOrigin(srcRect);
  auto sourceSize = MakeMTLSize(srcRect);
  MTLOrigin destinationOrigin = MTLOriginMake(static_cast<NSUInteger>(dstOffset.x),
                                             static_cast<NSUInteger>(dstOffset.y), 0);
  
  [blitEncoder copyFromTexture:metalSrcTexture->metalTexture()
                   sourceSlice:0
                   sourceLevel:0
                  sourceOrigin:sourceOrigin
                    sourceSize:sourceSize
                     toTexture:metalDstTexture->metalTexture()
              destinationSlice:0
              destinationLevel:0
             destinationOrigin:destinationOrigin];
  
  [blitEncoder endEncoding];
}

void MetalCommandEncoder::copyTextureToBuffer(std::shared_ptr<Texture> srcTexture, 
                                          const Rect& srcRect,
                                          std::shared_ptr<GPUBuffer> dstBuffer, 
                                          size_t dstOffset,
                                          size_t dstRowBytes) {
  if (!srcTexture || !dstBuffer) {
    return;
  }
  
  auto metalSrcTexture = std::static_pointer_cast<MetalTexture>(srcTexture);
  auto metalDstBuffer = std::static_pointer_cast<MetalBuffer>(dstBuffer);
  
  id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
  if (blitEncoder == nil) {
    return;
  }
  
  auto sourceOrigin = MakeMTLOrigin(srcRect);
  auto sourceSize = MakeMTLSize(srcRect);
  
  // Calculate bytes per row based on texture format
  auto bytesPerPixel = MetalDefines::GetBytesPerPixel(metalSrcTexture->metalTexture().pixelFormat);
  NSUInteger bytesPerRow = dstRowBytes > 0 ? static_cast<NSUInteger>(dstRowBytes) : 
                          static_cast<NSUInteger>(srcRect.width()) * bytesPerPixel;
  NSUInteger bytesPerImage = bytesPerRow * static_cast<NSUInteger>(srcRect.height());
  
  [blitEncoder copyFromTexture:metalSrcTexture->metalTexture()
                   sourceSlice:0
                   sourceLevel:0
                  sourceOrigin:sourceOrigin
                    sourceSize:sourceSize
                      toBuffer:metalDstBuffer->metalBuffer()
             destinationOffset:dstOffset
        destinationBytesPerRow:bytesPerRow
      destinationBytesPerImage:bytesPerImage];
  
  [blitEncoder endEncoding];
  
  metalDstBuffer->insertReadbackFence(commandBuffer);
}

void MetalCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  if (!texture) {
    return;
  }
  
  auto metalTexture = std::static_pointer_cast<MetalTexture>(texture);
  if (metalTexture->mipLevelCount() <= 1) {
    return;
  }
  
  id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
  if (blitEncoder == nil) {
    return;
  }
  [blitEncoder generateMipmapsForTexture:metalTexture->metalTexture()];
  [blitEncoder endEncoding];
}

std::shared_ptr<CommandBuffer> MetalCommandEncoder::onFinish() {
  return std::make_shared<MetalCommandBuffer>(commandBuffer);
}

}  // namespace tgfx