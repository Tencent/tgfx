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

#include <webgpu/webgpu.h>
#include "WebGPUResource.h"
#include "tgfx/gpu/CommandEncoder.h"

namespace tgfx {

class WebGPUGPU;

class WebGPUCommandEncoder : public CommandEncoder, public WebGPUResource {
 public:
  static std::shared_ptr<WebGPUCommandEncoder> Make(WebGPUGPU* gpu);

  GPU* gpu() const override;

  void copyTextureToTexture(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                            std::shared_ptr<Texture> dstTexture, const Point& dstOffset) override;

  void copyTextureToBuffer(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                           std::shared_ptr<GPUBuffer> dstBuffer, size_t dstOffset = 0,
                           size_t dstRowBytes = 0) override;

  void generateMipmapsForTexture(std::shared_ptr<Texture> texture) override;

  void onRelease(WebGPUGPU* gpu) override;

 protected:
  std::shared_ptr<RenderPass> onBeginRenderPass(const RenderPassDescriptor& descriptor) override;

  std::shared_ptr<CommandBuffer> onFinish() override;

 private:
  explicit WebGPUCommandEncoder(WebGPUGPU* gpu);

  WebGPUGPU* _gpu = nullptr;
  WGPUCommandEncoder commandEncoder = nullptr;

  friend class WebGPUGPU;
};

}  // namespace tgfx
