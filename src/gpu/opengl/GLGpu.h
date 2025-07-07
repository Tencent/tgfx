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

#include "gpu/Gpu.h"
#include "gpu/opengl/GLRenderPass.h"

namespace tgfx {
class GLGpu : public Gpu {
 public:
  static std::unique_ptr<Gpu> Make(Context* context);

  std::unique_ptr<TextureSampler> createSampler(int width, int height, PixelFormat format,
                                                int mipLevelCount) override;

  void deleteSampler(TextureSampler* sampler) override;

  void writePixels(const TextureSampler* sampler, Rect rect, const void* pixels,
                   size_t rowBytes) override;

  void bindTexture(int unitIndex, const TextureSampler* sampler, SamplerState samplerState = {});

  void copyRenderTargetToTexture(const RenderTarget* renderTarget, Texture* texture, int srcX,
                                 int srcY) override;

  void resolveRenderTarget(RenderTarget* renderTarget, const Rect& bounds) override;

  void regenerateMipmapLevels(const TextureSampler* sampler) override;

  bool insertSemaphore(Semaphore* semaphore) override;

  bool waitSemaphore(const Semaphore* semaphore) override;

  bool submitToGpu(bool syncCpu) override;

 private:
  explicit GLGpu(Context* context) : Gpu(context) {
  }
};
}  // namespace tgfx
