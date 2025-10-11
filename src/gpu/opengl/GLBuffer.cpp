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
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLBuffer::GLBuffer(std::shared_ptr<GLInterface> interface, unsigned bufferID, size_t size,
                   uint32_t usage)
    : GPUBuffer(size, usage), _interface(std::move(interface)), _bufferID(bufferID) {
}

unsigned GLBuffer::GetTarget(uint32_t usage) {
  if (usage & GPUBufferUsage::READBACK) {
    return GL_PIXEL_PACK_BUFFER;
  }
  if (usage & GPUBufferUsage::VERTEX) {
    return GL_ARRAY_BUFFER;
  }
  if (usage & GPUBufferUsage::INDEX) {
    return GL_ELEMENT_ARRAY_BUFFER;
  }
  if (usage & GPUBufferUsage::UNIFORM) {
    return GL_UNIFORM_BUFFER;
  }
  return 0;
}

bool GLBuffer::isReady() const {
  if (readbackFence == nullptr) {
    return true;
  }
  auto gl = _interface->functions();
  auto result = gl->clientWaitSync(readbackFence, 0, 0);
  return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}

void* GLBuffer::map(size_t offset, size_t size) {
  if (size == 0) {
    LOGE("GLBuffer::map() size cannot be 0!");
    return nullptr;
  }
  if (size == GPU_BUFFER_WHOLE_SIZE) {
    size = _size - offset;
  }
  if (offset + size > _size) {
    LOGE("GLBuffer::map() range out of bounds!");
    return nullptr;
  }

  auto gl = _interface->functions();
  if (gl->mapBufferRange == nullptr) {
    return nullptr;
  }
  // Avoid using GL_MAP_UNSYNCHRONIZED_BIT with READBACK buffers to ensure the GPU has finished
  // writing before reading.
  unsigned access = _usage & GPUBufferUsage::READBACK
                        ? GL_MAP_READ_BIT
                        : GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
  auto target = GetTarget(_usage);
  DEBUG_ASSERT(target != 0);
  gl->bindBuffer(target, _bufferID);
  return gl->mapBufferRange(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size),
                            access);
}

void GLBuffer::unmap() {
  auto gl = _interface->functions();
  if (gl->mapBufferRange != nullptr) {
    auto target = GetTarget(_usage);
    DEBUG_ASSERT(target != 0);
    gl->bindBuffer(target, _bufferID);
    gl->unmapBuffer(target);
  }
}

void GLBuffer::insertReadbackFence() {
  if (!_interface->caps()->semaphoreSupport) {
    return;
  }
  auto gl = _interface->functions();
  if (readbackFence != nullptr) {
    gl->deleteSync(readbackFence);
  }
  readbackFence = gl->fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void GLBuffer::onRelease(GLGPU* gpu) {
  DEBUG_ASSERT(gpu != nullptr);
  auto gl = gpu->functions();
  if (_bufferID > 0) {
    gl->deleteBuffers(1, &_bufferID);
    _bufferID = 0;
  }
  if (readbackFence != nullptr) {
    gl->deleteSync(readbackFence);
    readbackFence = nullptr;
  }
}
}  // namespace tgfx
