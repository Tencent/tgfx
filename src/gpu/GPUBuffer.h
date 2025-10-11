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

#include <cstddef>
#include <cstdint>
#include <limits>

namespace tgfx {
static constexpr size_t GPU_BUFFER_WHOLE_SIZE = std::numeric_limits<size_t>::max();

class GPU;

/**
 * GPUBufferUsage defines the usage flags for GPU buffers.
 */
class GPUBufferUsage {
 public:
  /**
   * The buffer can be used as an index buffer.
   */
  static constexpr uint32_t INDEX = 0x10;

  /**
   * The buffer can be used as a vertex buffer.
   */
  static constexpr uint32_t VERTEX = 0x20;

  /**
   * The buffer can be used as a uniform buffer.
   */
  static constexpr uint32_t UNIFORM = 0x40;

  /**
   * The buffer can be used as a readback buffer, allowing data to be transferred from the GPU back
   * to the CPU.
   */
  static constexpr uint32_t READBACK = 0x800;
};

/**
 * GPUBuffer represents a block of GPU memory used to store raw data for GPU operations.
 */
class GPUBuffer {
 public:
  virtual ~GPUBuffer() = default;

  /**
   * Returns the size of the buffer in bytes. This size is determined at the time of buffer creation
   * and cannot be changed later.
   */
  size_t size() const {
    return _size;
  }

  /**
   * Returns the bitwise flags that indicate the original usage options set when the GPUBuffer was
   * created. The returned value is the sum of the decimal values for each flag. See GPUBufferUsage
   * for more details.
   */
  uint32_t usage() const {
    return _usage;
  }

  /**
   * Checks if the GPUBuffer is ready for access. For readback buffers, this means the data transfer
   * from the GPU to the CPU has finished. For other buffer types, this usually returns true
   * immediately after creation.
   */
  virtual bool isReady() const = 0;

  /**
   * Maps the whole GPUBuffer, allowing the CPU to directly access its memory for reading or writing.
   * For readback buffers, this method may block until the data transfer is complete, if the backend
   * supports blocking. After you are done, call unmap() to release the mapping and let the GPU
   * access the buffer again. Returns nullptr if mapping fails, or if the readback buffer is not
   * ready and blocking is not supported.
   */
  void* map() {
    return map(0, GPU_BUFFER_WHOLE_SIZE);
  }

  /**
   * Mapping a range of the GPUBuffer, allowing the CPU to directly access a specific portion of its
   * memory for reading or writing. For readback buffers, this method may block until the data
   * transfer is complete, if the backend supports blocking. After you are done, call unmap() to
   * release the mapping and let the GPU access the buffer again.
   * @param offset The offset in bytes from the start of the buffer where the mapping should begin.
   * @param size The size in bytes of the memory region to map.
   * @return A pointer to the mapped memory region, or nullptr if mapping fails, or if the readback
   * buffer is not ready and blocking is unsupported.
   */
  virtual void* map(size_t offset, size_t size) = 0;

  /**
   * Unmaps the GPUBuffer, making its contents available for use by the GPU again.
   */
  virtual void unmap() = 0;

 protected:
  size_t _size = 0;
  uint32_t _usage = 0;

  GPUBuffer(size_t size, uint32_t usage) : _size(size), _usage(usage) {
  }
};
}  // namespace tgfx
