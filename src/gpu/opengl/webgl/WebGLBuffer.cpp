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
#include "gpu/GPU.h"
#include "gpu/opengl/GLGPU.h"

namespace tgfx {
WebGLBuffer::WebGLBuffer(std::shared_ptr<GLInterface> interface, unsigned bufferID, size_t size,
                         uint32_t usage)
    : GPUBuffer(size, usage), _interface(std::move(interface)), uniqueID(UniqueID::Next()),
      _bufferID(bufferID) {
  dataAddress = malloc(_size);
}

WebGLBuffer::~WebGLBuffer() {
  if (dataAddress != nullptr) {
    free(dataAddress);
    dataAddress = nullptr;
  }
}

unsigned WebGLBuffer::target() const {
  if (_usage & GPUBufferUsage::VERTEX) {
    return GL_ARRAY_BUFFER;
  }
  if (_usage & GPUBufferUsage::INDEX) {
    return GL_ELEMENT_ARRAY_BUFFER;
  }
  if (_usage & GPUBufferUsage::UNIFORM) {
    return GL_UNIFORM_BUFFER;
  }
  LOGE("WebGLBuffer::target() invalid buffer usage!");
  return 0;
}

void* WebGLBuffer::map(size_t offset, size_t size) {
  if (dataAddress != nullptr) {
    subDataOffset = offset;
    subDataSize = size;

    return static_cast<uint8_t*>(dataAddress) + offset;
  }

  return nullptr;
}

void WebGLBuffer::unmap() {
  auto bufferTarget = target();
  auto gl = _interface->functions();

  gl->bindBuffer(bufferTarget, _bufferID);
  gl->bufferSubData(bufferTarget, static_cast<GLintptr>(subDataOffset),
                    static_cast<GLsizeiptr>(subDataSize),
                    static_cast<uint8_t*>(dataAddress) + subDataOffset);
}

void WebGLBuffer::onRelease(GLGPU* gpu) {
  DEBUG_ASSERT(gpu != nullptr);
  if (_bufferID > 0) {
    auto gl = gpu->functions();
    gl->deleteBuffers(1, &_bufferID);
    _bufferID = 0;
  }
}
}  // namespace tgfx
