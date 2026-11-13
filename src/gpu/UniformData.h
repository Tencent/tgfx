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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>

namespace tgfx {

/**
 * One stage's uniform block: a byte range the writers fill through the layout-derived offsets and
 * the upload hands to the GPU. The precompiled route addresses every field by the derived
 * layout's offset, so a block is a size and a writable buffer — there is no name table here.
 */
class UniformData {
 public:
  /**
   * Sets an external memory buffer for writing uniform data.
   * On platforms with UBO support, the buffer points to memory mapped from a GPU UBO object.
   * On platforms without UBO support, the buffer points to CPU memory.
   */
  void setBuffer(void* buffer);

  /// The backing bytes the layout-addressed writers (ParamBuffer) memcpy into; null before
  /// setBuffer. The offsets come from the same derived layout the assembled text declared.
  uint8_t* writableBytes() const {
    return _buffer;
  }

  /**
   * Returns the size of the uniform data in bytes.
   */
  size_t size() const {
    return bufferSize;
  }

 private:
  /// A block of the given byte size: the precompiled route addresses every field through its
  /// derived layout's offsets, so the block needs only its size and a buffer.
  explicit UniformData(size_t size);

  uint8_t* _buffer = nullptr;
  size_t bufferSize = 0;

  friend class PrecompiledProgramCreator;
};

}  // namespace tgfx
