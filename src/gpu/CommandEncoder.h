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
#include "gpu/GPUFence.h"
#include "gpu/GPUTexture.h"

namespace tgfx {

class RenderPass;
class RenderPassDescriptor;

/**
 * CommandEncoder represents an interface for collecting a sequence of GPU commands to be issued to
 * the GPU.
 */
class CommandEncoder {
 public:
  virtual ~CommandEncoder() = default;

  /**
   * Begins a render pass using the specified RenderPassDescriptor. Returns a RenderPass object to
   * control the rendering process, or nullptr if the descriptor is invalid. Only one render pass
   * can be active at a time; to start a new one, you must first call end() on the previous render
   * pass.
   */
  std::shared_ptr<RenderPass> beginRenderPass(const RenderPassDescriptor& descriptor);

  /**
   * Copies a region from the source GPUTexture to a region of the destination GPUTexture. If
   * the texture has mipmaps, you should call the generateMipmapsForTexture() method after copying,
   * as mipmaps will not be generated automatically.
   * @param srcTexture The source frame buffer to copy from.
   * @param srcRect The rectangle region of the source texture to copy from.
   * @param dstTexture The destination texture to copy to.
   * @param dstOffset The offset in the destination texture where the copied region will be placed.
   */
  virtual void copyTextureToTexture(std::shared_ptr<GPUTexture> srcTexture, const Rect& srcRect,
                                    std::shared_ptr<GPUTexture> dstTexture,
                                    const Point& dstOffset) = 0;

  /**
   * Encodes a command that generates mipmaps for the specified GPUTexture from the base level to
   * the highest level. This method only has an effect if the texture was created with mipmap
   * enabled.
   */
  virtual void generateMipmapsForTexture(std::shared_ptr<GPUTexture> texture) = 0;

  /**
   * Inserts a signal GPUFence into the command encoder. This is used to notify other
   * synchronization points once the preceding GPU commands have finished executing. Returns nullptr
   * if the GPUFence cannot be created or inserted.
   */
  virtual std::shared_ptr<GPUFence> insertFence() = 0;

  /**
   * Makes subsequent commands added to the command encoder to wait until the specified GPUFence is
   * signaled.
   */
  virtual void waitForFence(std::shared_ptr<GPUFence> fence) = 0;

  /**
   * Finalizes command encoding and returns a CommandBuffer with all recorded commands. You can then
   * submit the CommandBuffer to the GPU for execution using GPU::submit(). Returns nullptr if no
   * commands were recorded or if encoding failed, for example, if an active render pass was not
   * properly ended.
   */
  std::shared_ptr<CommandBuffer> finish();

 protected:
  virtual std::shared_ptr<RenderPass> onBeginRenderPass(const RenderPassDescriptor& descriptor) = 0;

  virtual std::shared_ptr<CommandBuffer> onFinish() = 0;

 private:
  std::shared_ptr<RenderPass> activeRenderPass = nullptr;
};
}  // namespace tgfx
