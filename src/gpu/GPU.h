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

#include <memory>
#include "gpu/Semaphore.h"
#include "gpu/TextureSampler.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class RenderTarget;
class Texture;

class GPU {
 public:
  virtual ~GPU() = default;

  Context* getContext() {
    return context;
  }

  virtual void copyRenderTargetToTexture(const RenderTarget* renderTarget, Texture* texture,
                                         int srcX, int srcY) = 0;

  virtual std::shared_ptr<Semaphore> insertSemaphore() = 0;

  virtual void waitSemaphore(const Semaphore* semaphore) = 0;

  virtual bool submitToGPU(bool syncCpu) = 0;

 protected:
  Context* context;

  explicit GPU(Context* context) : context(context) {
  }
};
}  // namespace tgfx
