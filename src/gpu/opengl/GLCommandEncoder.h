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

#include "gpu/CommandEncoder.h"

namespace tgfx {
class GLGPU;

class GLCommandEncoder : public CommandEncoder {
 public:
  explicit GLCommandEncoder(GLGPU* gpu) : gpu(gpu) {
  }

  void copyTextureToTexture(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                            std::shared_ptr<Texture> dstTexture, const Point& dstOffset) override;

  void copyTextureToBuffer(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                           std::shared_ptr<GPUBuffer> dstBuffer, size_t dstOffset,
                           size_t dstRowBytes) override;

  void generateMipmapsForTexture(std::shared_ptr<Texture> texture) override;

 protected:
  std::shared_ptr<RenderPass> onBeginRenderPass(const RenderPassDescriptor& descriptor) override;

  std::shared_ptr<CommandBuffer> onFinish() override;

 private:
  GLGPU* gpu = nullptr;
};
}  // namespace tgfx
