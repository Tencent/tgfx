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

namespace tgfx {
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

 protected:
  size_t _size = 0;
  uint32_t _usage = 0;

  GPUBuffer(size_t size, uint32_t usage) : _size(size), _usage(usage) {
  }
};
}  // namespace tgfx
