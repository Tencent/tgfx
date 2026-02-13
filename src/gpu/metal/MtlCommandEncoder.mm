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

#include "MtlCommandEncoder.h"
#include "MtlGPU.h"
#include "MtlCommandQueue.h"
#include "MtlCommandBuffer.h"
#include "MtlRenderPass.h"
#include "MtlTexture.h"
#include "MtlBuffer.h"
#include "MtlDefines.h"

namespace tgfx {

std::shared_ptr<MtlCommandEncoder> MtlCommandEncoder::Make(MtlGPU* gpu) {
  if (!gpu) {
    return nullptr;
  }
  
  auto encoder = gpu->makeResource<MtlCommandEncoder>(gpu);
  if (encoder->commandBuffer == nil) {
    return nullptr;
  }
  return encoder;
}

MtlCommandEncoder::MtlCommandEncoder(MtlGPU* gpu) : _gpu(gpu) {
  auto queue = static_cast<MtlCommandQueue*>(gpu->queue());
  commandBuffer = [[queue->mtlCommandQueue() commandBuffer] retain];
  queue->encodePendingWait(commandBuffer);
}

void MtlCommandEncoder::onRelease(MtlGPU*) {
  if (commandBuffer != nil) {
    [commandBuffer release];
    commandBuffer = nil;
  }
}

GPU* MtlCommandEncoder::gpu() const {
  return _gpu;
}

std::shared_ptr<RenderPass> MtlCommandEncoder::onBeginRenderPass(
    const RenderPassDescriptor& descriptor) {
  return MtlRenderPass::Make(this, descriptor);
}

void MtlCommandEncoder::copyTextureToTexture(std::shared_ptr<Texture> srcTexture, 
                                           const Rect& srcRect,
                                           std::shared_ptr<Texture> dstTexture, 
                                           const Point& dstOffset) {
  if (!srcTexture || !dstTexture) {
    return;
  }
  
  auto mtlSrcTexture = std::static_pointer_cast<MtlTexture>(srcTexture);
  auto mtlDstTexture = std::static_pointer_cast<MtlTexture>(dstTexture);
  
  id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
  
  MTLOrigin sourceOrigin = MTLOriginMake(static_cast<NSUInteger>(srcRect.x()),
                                        static_cast<NSUInteger>(srcRect.y()), 0);
  MTLSize sourceSize = MTLSizeMake(static_cast<NSUInteger>(srcRect.width()),
                                  static_cast<NSUInteger>(srcRect.height()), 1);
  MTLOrigin destinationOrigin = MTLOriginMake(static_cast<NSUInteger>(dstOffset.x),
                                             static_cast<NSUInteger>(dstOffset.y), 0);
  
  [blitEncoder copyFromTexture:mtlSrcTexture->mtlTexture()
                   sourceSlice:0
                   sourceLevel:0
                  sourceOrigin:sourceOrigin
                    sourceSize:sourceSize
                     toTexture:mtlDstTexture->mtlTexture()
              destinationSlice:0
              destinationLevel:0
             destinationOrigin:destinationOrigin];
  
  [blitEncoder endEncoding];
}

void MtlCommandEncoder::copyTextureToBuffer(std::shared_ptr<Texture> srcTexture, 
                                          const Rect& srcRect,
                                          std::shared_ptr<GPUBuffer> dstBuffer, 
                                          size_t dstOffset,
                                          size_t dstRowBytes) {
  if (!srcTexture || !dstBuffer) {
    return;
  }
  
  auto mtlSrcTexture = std::static_pointer_cast<MtlTexture>(srcTexture);
  auto mtlDstBuffer = std::static_pointer_cast<MtlBuffer>(dstBuffer);
  
  id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
  
  MTLOrigin sourceOrigin = MTLOriginMake(static_cast<NSUInteger>(srcRect.x()),
                                        static_cast<NSUInteger>(srcRect.y()), 0);
  MTLSize sourceSize = MTLSizeMake(static_cast<NSUInteger>(srcRect.width()),
                                  static_cast<NSUInteger>(srcRect.height()), 1);
  
  // Calculate bytes per row based on texture format
  auto bytesPerPixel = MtlDefines::GetBytesPerPixel(mtlSrcTexture->mtlTexture().pixelFormat);
  NSUInteger bytesPerRow = dstRowBytes > 0 ? static_cast<NSUInteger>(dstRowBytes) : 
                          static_cast<NSUInteger>(srcRect.width()) * bytesPerPixel;
  NSUInteger bytesPerImage = bytesPerRow * static_cast<NSUInteger>(srcRect.height());
  
  [blitEncoder copyFromTexture:mtlSrcTexture->mtlTexture()
                   sourceSlice:0
                   sourceLevel:0
                  sourceOrigin:sourceOrigin
                    sourceSize:sourceSize
                      toBuffer:mtlDstBuffer->mtlBuffer()
             destinationOffset:dstOffset
        destinationBytesPerRow:bytesPerRow
      destinationBytesPerImage:bytesPerImage];
  
  [blitEncoder endEncoding];
  
  mtlDstBuffer->insertReadbackFence(commandBuffer);
}

void MtlCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  if (!texture) {
    return;
  }
  
  auto mtlTexture = std::static_pointer_cast<MtlTexture>(texture);
  if (mtlTexture->mipLevelCount() <= 1) {
    return;
  }
  
  id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
  [blitEncoder generateMipmapsForTexture:mtlTexture->mtlTexture()];
  [blitEncoder endEncoding];
}

std::shared_ptr<CommandBuffer> MtlCommandEncoder::onFinish() {
  return std::make_shared<MtlCommandBuffer>(commandBuffer);
}

}  // namespace tgfx