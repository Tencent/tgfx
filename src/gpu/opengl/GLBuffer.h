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

#include "core/utils/UniqueID.h"
#include "gpu/GPUBuffer.h"
#include "gpu/opengl/GLInterface.h"

namespace tgfx {
/**
 * GLBuffer is a GPUBuffer implementation for OpenGL. It encapsulates an OpenGL buffer object and
 * provides methods to access its properties and release its resources.
 */
class GLBuffer : public GPUBuffer {
 public:
  /**
   * Creates a new GLBuffer with the specified size and usage flags.
   */
  GLBuffer(unsigned bufferID, size_t size, uint32_t usage)
      : GPUBuffer(size, usage), uniqueID(UniqueID::Next()), _bufferID(bufferID) {
  }

  /**
   * Returns the OpenGL target for this buffer based on its usage flags.
   */
  unsigned target() const;

  /**
   * Returns the OpenGL buffer ID associated with this buffer.
   */
  unsigned bufferID() const {
    return _bufferID;
  }

  /**
   * Returns a pointer to the mapped range of the uniform buffer for CPU access.
   * If the uniform buffer is already mapped, it returns the existing mapped pointer.
   * Otherwise, it maps the buffer range and returns the pointer.
   * The offset and size must be within the buffer's size and properly aligned.
   * @param gpu The GPU instance used for mapping.
   * @param offset The offset within the buffer to start mapping.
   * @param size The size of the range to map.
   * @return A pointer to the mapped range, or nullptr if mapping fails.
   */
  void* getMappedRange(GPU* gpu, size_t offset, size_t size) override;

  /**
   * Unmaps the uniform buffer, making it inaccessible for CPU access.
   * If the buffer is not currently mapped, this function does nothing.
   */
  void unmap(GPU* gpu) override;

  void release(GPU* gpu) override;

 private:
  uint32_t uniqueID = 0;
  unsigned _bufferID = 0;

  friend class GLState;
};
}  // namespace tgfx
