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
#include "gpu/opengl/GLInterface.h"

namespace tgfx {
class GLCommandEncoder : public CommandEncoder {
 public:
  explicit GLCommandEncoder(std::shared_ptr<GLInterface> interface)
      : interface(std::move(interface)) {
  }

  void copyFrameBufferToTexture(GPUFrameBuffer* frameBuffer, const Point& srcOffset,
                                GPUTexture* texture, const Rect& dstRect) override;

  void generateMipmapsForTexture(GPUTexture* texture) override;

  BackendSemaphore insertSemaphore() override;

  void waitSemaphore(const BackendSemaphore& semaphore) override;

 protected:
  std::shared_ptr<RenderPass> onBeginRenderPass(std::shared_ptr<RenderTarget> renderTarget,
                                                bool resolveMSAA) override;

  std::shared_ptr<CommandBuffer> onFinish() override;

 private:
  std::shared_ptr<GLInterface> interface = nullptr;
};
}  // namespace tgfx
