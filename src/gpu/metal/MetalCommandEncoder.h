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
#include "tgfx/gpu/CommandEncoder.h"
#include "MetalResource.h"

namespace tgfx {

class MetalGPU;

/**
 * Metal command encoder implementation.
 */
class MetalCommandEncoder : public CommandEncoder, public MetalResource {
 public:
  static std::shared_ptr<MetalCommandEncoder> Make(MetalGPU* gpu);

  /**
   * Returns the Metal command buffer.
   */
  id<MTLCommandBuffer> metalCommandBuffer() const {
    return commandBuffer;
  }

  // CommandEncoder interface implementation
  GPU* gpu() const override;
  
  std::shared_ptr<RenderPass> onBeginRenderPass(
      const RenderPassDescriptor& descriptor) override;

  void copyTextureToTexture(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                           std::shared_ptr<Texture> dstTexture, const Point& dstOffset) override;

  void copyTextureToBuffer(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                          std::shared_ptr<GPUBuffer> dstBuffer, size_t dstOffset = 0,
                          size_t dstRowBytes = 0) override;

  void generateMipmapsForTexture(std::shared_ptr<Texture> texture) override;

 protected:
  std::shared_ptr<CommandBuffer> onFinish() override;
  void onRelease(MetalGPU* gpu) override;

 private:
  explicit MetalCommandEncoder(MetalGPU* gpu);
  ~MetalCommandEncoder() override = default;

  MetalGPU* _gpu = nullptr;
  id<MTLCommandBuffer> commandBuffer = nil;
  
  friend class MetalGPU;
};

}  // namespace tgfx