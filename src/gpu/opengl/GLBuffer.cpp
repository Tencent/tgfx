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

#include "GLBuffer.h"
#include <limits>
#include "GLGPU.h"
#include "GLUtil.h"

namespace tgfx {
GLBuffer::GLBuffer(std::shared_ptr<GLInterface> interface, unsigned bufferID, size_t size,
                   uint32_t usage)
    : GPUBuffer(size, usage), _interface(std::move(interface)), uniqueID(UniqueID::Next()),
      _bufferID(bufferID) {
#if defined(__EMSCRIPTEN__)
  if (usage & GPUBufferUsage::UNIFORM) {
    dataAddress = malloc(_size);
  }
#else
  if ((usage & GPUBufferUsage::UNIFORM) && !_interface->caps()->shaderCaps()->uboSupport) {
    dataAddress = malloc(_size);
  }
#endif
}

GLBuffer::~GLBuffer() {
  if (dataAddress != nullptr) {
    free(dataAddress);
    dataAddress = nullptr;
  }
}

unsigned GLBuffer::target() const {
  if (_usage & GPUBufferUsage::VERTEX) {
    return GL_ARRAY_BUFFER;
  }
  if (_usage & GPUBufferUsage::INDEX) {
    return GL_ELEMENT_ARRAY_BUFFER;
  }
  if (_usage & GPUBufferUsage::UNIFORM) {
    return GL_UNIFORM_BUFFER;
  }
  LOGE("GLBuffer::target() invalid buffer usage!");
  return 0;
}

void* GLBuffer::map(size_t offset, size_t size) {
  if (size == std::numeric_limits<size_t>::max()) {
    size = _size - offset;
  }

  DEBUG_ASSERT(offset + size <= _size);

  if (dataAddress != nullptr) {
    return static_cast<uint8_t*>(dataAddress) + offset;
  }

  auto gl = _interface->functions();
  if (gl->mapBufferRange != nullptr) {
    auto bufferTarget = target();
    gl->bindBuffer(bufferTarget, _bufferID);
    return gl->mapBufferRange(bufferTarget, static_cast<GLintptr>(offset),
                              static_cast<GLsizeiptr>(size),
                              GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
  }

  return nullptr;
}

void GLBuffer::unmap() {
  if (dataAddress != nullptr) {
    return;
  }

  auto gl = _interface->functions();
  if (gl->mapBufferRange != nullptr) {
    auto bufferTarget = target();
    gl->bindBuffer(bufferTarget, _bufferID);
    gl->unmapBuffer(bufferTarget);
  }
}

void GLBuffer::onRelease(GLGPU* gpu) {
  DEBUG_ASSERT(gpu != nullptr);
  if (_bufferID > 0) {
    auto gl = gpu->functions();
    gl->deleteBuffers(1, &_bufferID);
    _bufferID = 0;
  }
}
}  // namespace tgfx
