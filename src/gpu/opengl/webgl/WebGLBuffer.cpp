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

#include "WebGLBuffer.h"
#include <utility>
#include "gpu/GPU.h"
#include "gpu/opengl/GLGPU.h"

namespace tgfx {
WebGLBuffer::WebGLBuffer(std::shared_ptr<GLInterface> interface, unsigned bufferID, size_t size,
                         uint32_t usage)
    : GLBuffer(std::move(interface), bufferID, size, usage) {
}

WebGLBuffer::~WebGLBuffer() {
  if (bufferData != nullptr) {
    free(bufferData);
    bufferData = nullptr;
  }
}

void* WebGLBuffer::map(size_t offset, size_t size) {
  if (bufferData != nullptr) {
    LOGE(
        "WebGLBuffer::map() cannot be called repeatedly, you must call unmap() before mapping "
        "again.");
    return nullptr;
  }

  if (size == 0) {
    LOGE("WebGLBuffer::map() size cannot be 0!");
    return nullptr;
  }

  if (size == GPU_BUFFER_WHOLE_SIZE) {
    size = _size - offset;
  }

  if (offset + size > _size) {
    LOGE("WebGLBuffer::map() range out of bounds!");
    return nullptr;
  }

  bufferData = malloc(size);
  if (bufferData != nullptr) {
    subDataOffset = offset;
    subDataSize = size;

    return bufferData;
  }

  return nullptr;
}

void WebGLBuffer::unmap() {
  if (bufferData == nullptr) {
    return;
  }

  auto bufferTarget = target();
  auto gl = _interface->functions();

  gl->bindBuffer(bufferTarget, _bufferID);
  gl->bufferSubData(bufferTarget, static_cast<GLintptr>(subDataOffset),
                    static_cast<GLsizeiptr>(subDataSize), bufferData);

  free(bufferData);
  bufferData = nullptr;
}
}  // namespace tgfx
