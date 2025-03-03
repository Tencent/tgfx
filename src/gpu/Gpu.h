/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <memory>
#include "gpu/Semaphore.h"
#include "gpu/TextureSampler.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class RenderPass;

class RenderTarget;

class Texture;

class Gpu {
 public:
  virtual ~Gpu() = default;

  Context* getContext() {
    return context;
  }

  virtual std::unique_ptr<TextureSampler> createSampler(int width, int height, PixelFormat format,
                                                        int mipLevelCount) = 0;

  virtual void deleteSampler(TextureSampler* sampler) = 0;

  virtual void writePixels(const TextureSampler* sampler, Rect rect, const void* pixels,
                           size_t rowBytes) = 0;

  virtual void copyRenderTargetToTexture(const RenderTarget* renderTarget, Texture* texture,
                                         int srcX, int srcY) = 0;

  virtual void resolveRenderTarget(RenderTarget* renderTarget, const Rect& bounds) = 0;

  virtual void regenerateMipmapLevels(const TextureSampler* sampler) = 0;

  virtual bool insertSemaphore(Semaphore* semaphore) = 0;

  virtual bool waitSemaphore(const Semaphore* semaphore) = 0;

  virtual bool submitToGpu(bool syncCpu) = 0;

 protected:
  Context* context;

  explicit Gpu(Context* context) : context(context) {
  }
};
}  // namespace tgfx
