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

#include "gpu/GPUBuffer.h"
#include "gpu/opengl/GLInterface.h"
#include "gpu/opengl/GLResource.h"

namespace tgfx {
/**
 * GLBuffer is a GPUBuffer implementation for OpenGL. It encapsulates an OpenGL buffer object and
 * provides methods to access its properties and release its resources.
 */
class GLBuffer : public GPUBuffer, public GLResource {
 public:
  /**
   * Creates a new GLBuffer with the specified size and usage flags.
   */
  GLBuffer(std::shared_ptr<GLInterface> interface, unsigned bufferID, size_t size, uint32_t usage);

  ~GLBuffer() override;

  /**
   * Returns the OpenGL target for the buffer based on its usage flags.
   */
  static unsigned GetTarget(uint32_t usage);

  /**
   * Returns the OpenGL buffer ID associated with this buffer.
   */
  unsigned bufferID() const {
    return _bufferID;
  }

  bool isReady() const override;

  void* map(size_t offset, size_t size) override;

  void unmap() override;

  void insertReadbackFence();

 protected:
  std::shared_ptr<GLInterface> _interface = nullptr;
  unsigned _bufferID = 0;
  void* readbackFence = nullptr;
  void* dataAddress = nullptr;

  void onRelease(GLGPU* gpu) override;
};
}  // namespace tgfx
