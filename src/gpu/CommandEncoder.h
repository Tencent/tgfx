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

#include "gpu/RenderPass.h"
#include "gpu/RenderTarget.h"
#include "gpu/Semaphore.h"

namespace tgfx {
/**
 * CommandEncoder represents an interface for collecting a sequence of GPU commands to be issued to
 * the GPU.
 */
class CommandEncoder {
 public:
  virtual ~CommandEncoder() = default;

  /**
   * Begins a render pass for the specified render target. Returns a RenderPass object that can be
   * used to control the rendering process. If the render target is null, it will return nullptr.
   * If the resolveMSAA parameter is true, it will resolve the MSAA (multisampling antialiasing) for
   * the render target (if it has MSAA) when the end() method is called. Note that only one render
   * pass can be active at a time. To begin a new render pass, you must first call end() on the
   * previous one.
   */
  std::shared_ptr<RenderPass> beginRenderPass(std::shared_ptr<RenderTarget> renderTarget,
                                              bool resolveMSAA = true);

  /**
   * Copy the contents of the specified render target to a texture at the specified source
   * coordinates.
   */
  virtual void copyRenderTargetToTexture(const RenderTarget* renderTarget, Texture* texture,
                                         int srcX, int srcY) = 0;

  /**
   * Generates mipmaps for the given texture sampler based on its current content.
   */
  virtual void generateMipmapsForTexture(TextureSampler* sampler) = 0;

  /**
   * Inserts a signal semaphore into the command encoder. This is used to notify other
   * synchronization points once the preceding GPU commands have finished executing. Returns nullptr
   * if the semaphore cannot be created or inserted.
   */
  virtual BackendSemaphore insertSemaphore() = 0;

  /**
   * Makes subsequent commands added to the command encoder to wait until the specified semaphore is
   * signaled.
   */
  virtual void waitSemaphore(const BackendSemaphore& semaphore) = 0;

 protected:
  virtual std::shared_ptr<RenderPass> onBeginRenderPass(std::shared_ptr<RenderTarget> renderTarget,
                                                        bool resolveMSAA) = 0;

 private:
  std::shared_ptr<RenderPass> activeRenderPass = nullptr;
};
}  // namespace tgfx
