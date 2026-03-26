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

#include <chrono>
#include <memory>
#include "tgfx/core/Rect.h"
#include "tgfx/gpu/CommandBuffer.h"
#include "tgfx/gpu/GPUBuffer.h"
#include "tgfx/gpu/Semaphore.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
class Context;

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
   */
  virtual void writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset, const void* data,
                           size_t size) = 0;

  /**
   * Writes pixel data to the texture within the specified rectangle. The pixel data must match
   * the texture's pixel format, and the rectangle must be fully contained within the texture's
   * dimensions. If the texture has mipmaps, you should call CommandEncoder's
   * generateMipmapsForTexture() method after writing the pixels, as mipmaps will not be generated
   * automatically.
   */
  virtual void writeTexture(std::shared_ptr<Texture> texture, const Rect& rect, const void* pixels,
                            size_t rowBytes) = 0;

  /**
   * Schedules the execution of the specified command buffer on the GPU.
   */
  virtual void submit(std::shared_ptr<CommandBuffer> commandBuffer) = 0;

  /**
   * Inserts a Semaphore into the command queue. This allows other synchronization points to be
   * notified when all previous GPU commands have finished executing. Returns nullptr if the
   * Semaphore cannot be inserted due to lack of support (for example, on WebGPU).
   */
  virtual std::shared_ptr<Semaphore> insertSemaphore() = 0;

  /**
   * Inserts a GPU wait operation into the command queue, making the GPU wait until the specified
   * semaphore is signaled before executing subsequent commands.
   */
  virtual void waitSemaphore(std::shared_ptr<Semaphore> semaphore) = 0;

  /**
   * Blocks the current thread until all previously submitted commands in this queue have completed
   * execution on the GPU.
   */
  virtual void waitUntilCompleted() = 0;

 protected:
  /**
   * Returns the time point recorded at the start of the current frame. This value is set by
   * advanceFrameTime() and is used to timestamp resources and uniform buffer packets during
   * encoding. Only accessed on the context-lock thread.
   */
  std::chrono::steady_clock::time_point frameTime() const {
    return _frameTime;
  }

  /**
   * Returns the time point of the most recent frame whose GPU commands have fully completed.
   * Subclasses should override this to reflect asynchronous GPU completion (e.g., via Metal
   * completion handlers). The default implementation returns the current frame time, which assumes
   * synchronous execution (e.g., OpenGL after glFlush). Only accessed on the context-lock thread.
   */
  virtual std::chrono::steady_clock::time_point completedFrameTime() const {
    return _frameTime;
  }

  /**
   * Records the current wall-clock time as the start of a new frame. Called once per frame before
   * encoding begins. Only accessed on the context-lock thread.
   */
  void advanceFrameTime() {
    _frameTime = std::chrono::steady_clock::now();
  }

  /**
   * The time point recorded at the start of the current frame.
   */
  std::chrono::steady_clock::time_point _frameTime = {};

 private:
  friend class Context;
  friend class GlobalCache;
  friend class GPU;
  friend class ResourceCache;
};
}  // namespace tgfx
