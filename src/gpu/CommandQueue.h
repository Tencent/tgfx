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

#include <memory>
#include "gpu/CommandBuffer.h"
#include "gpu/GPUBuffer.h"
#include "gpu/GPUFence.h"
#include "gpu/GPUTexture.h"

namespace tgfx {
/**
 * CommandQueue is an interface for managing the execution of encoded commands on the GPU.
 * The primary queue can be accessed via the GPU::queue() method.
 */
class CommandQueue {
 public:
  virtual ~CommandQueue() = default;

  /**
   * Writes data to the specified GPUBuffer at the given offset. The size of the data must not
   * exceed the size of the buffer.
   * @param buffer The GPUBuffer to write to.
   * @param bufferOffset The offset in the buffer where the data should be written.
   * @param data Pointer to the data to write.
   * @param size The size of the data in bytes.
   * @return true if the write operation was successful, false otherwise.
   */
  virtual bool writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset, const void* data,
                           size_t size) = 0;

  /**
   * Writes pixel data to the GPUTexture within the specified rectangle. The pixel data must match
   * the texture's pixel format, and the rectangle must be fully contained within the texture's
   * dimensions. If the texture has mipmaps, you should call CommandEncoder's
   * generateMipmapsForTexture() method after writing the pixels, as mipmaps will not be generated
   * automatically.
   */
  virtual void writeTexture(std::shared_ptr<GPUTexture> texture, const Rect& rect,
                            const void* pixels, size_t rowBytes) = 0;

  /**
   * Copies pixel data from the GPUTexture within the specified rectangle into the provided memory
   * buffer. The buffer must be large enough to hold all the data for the rectangle. The rectangle
   * must be entirely within the frame buffer's dimensions. Returns true if the read operation
   * succeeds, false otherwise.
   */
  virtual bool readTexture(std::shared_ptr<GPUTexture> texture, const Rect& rect, void* pixels,
                           size_t rowBytes) const = 0;

  /**
   * Schedules the execution of the specified command buffer on the GPU.
   */
  virtual void submit(std::shared_ptr<CommandBuffer> commandBuffer) = 0;

  /**
   * Inserts a GPU fence into the command queue. The fence can be used to synchronize CPU and GPU
   * operations, allowing you to check or wait for the completion of previously submitted GPU
   * commands. Returns a shared pointer to the GPUFence, or nullptr if fence insertion failed.
   */
  virtual std::shared_ptr<GPUFence> insertFence() = 0;

  /**
   * Inserts a GPU wait operation into the command queue, making the GPU wait until the specified
   * fence is signaled before executing subsequent commands.
   */
  virtual void waitForFence(std::shared_ptr<GPUFence> fence) = 0;

  /**
   * Blocks the current thread until all previously submitted commands in this queue have completed
   * execution on the GPU.
   */
  virtual void waitUntilCompleted() = 0;
};
}  // namespace tgfx
