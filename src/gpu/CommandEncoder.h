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

#include "gpu/CommandBuffer.h"
#include "gpu/GPUTexture.h"
#include "gpu/RenderPass.h"
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
                                              std::optional<Color> clearColor = std::nullopt,
                                              bool resolveMSAA = true);

  /**
   * Copies a region from the source GPUTexture to a region of the destination GPUTexture. If
   * the texture has mipmaps, you should call the generateMipmapsForTexture() method after copying,
   * as mipmaps will not be generated automatically.
   * @param srcTexture The source frame buffer to copy from.
   * @param srcRect The rectangle region of the source texture to copy from.
   * @param dstTexture The destination texture to copy to.
   * @param dstOffset The offset in the destination texture where the copied region will be placed.
   */
  virtual void copyTextureToTexture(GPUTexture* srcTexture, const Rect& srcRect,
                                    GPUTexture* dstTexture, const Point& dstOffset) = 0;

  /**
   * Encodes a command that generates mipmaps for the specified GPUTexture from the base level to
   * the highest level. This method only has an effect if the texture was created with mipmap
   * enabled.
   */
  virtual void generateMipmapsForTexture(GPUTexture* texture) = 0;

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

  /**
   * Completes the command encoding process and returns a CommandBuffer containing all recorded
   * commands. The CommandBuffer can then be submitted to the GPU for execution using the
   * GPU::submit() method. Returns nullptr if no commands were recorded or if the encoding process
   * failed.
   */
  std::shared_ptr<CommandBuffer> finish();

 protected:
  virtual std::shared_ptr<RenderPass> onBeginRenderPass(std::shared_ptr<RenderTarget> renderTarget,
                                                        bool resolveMSAA) = 0;

  virtual std::shared_ptr<CommandBuffer> onFinish() = 0;

 private:
  std::shared_ptr<RenderPass> activeRenderPass = nullptr;
};
}  // namespace tgfx
