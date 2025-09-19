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
#include "GLGPU.h"
#include "GLUtil.h"

namespace tgfx {
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

void* GLBuffer::getMappedRange(GPU* gpu, size_t offset, size_t size) {
  if (_mapState == GPUBufferMapState::MAPPED) {
    return _mappedRange;
  }

  if (gpu == nullptr) {
    return nullptr;
  }

  if (offset + size > _size) {
    LOGE("GLBuffer::getMappedRange() out of range!");
    return nullptr;
  }

  auto bufferTarget = target();
  if (bufferTarget == GL_UNIFORM_BUFFER) {
    auto uboOffsetAlignment = static_cast<size_t>(gpu->caps()->shaderCaps()->uboOffsetAlignment);
    if (uboOffsetAlignment <= 0 || offset % uboOffsetAlignment != 0) {
      LOGE("GLBuffer::getMappedRange() invalid UBO offset:%zu, must be aligned to %zu bytes",
           offset, uboOffsetAlignment);
      return nullptr;
    }
  } else if (bufferTarget == GL_ELEMENT_ARRAY_BUFFER) {
    if (offset % 4 != 0) {
      LOGE("GLBuffer::getMappedRange() EBO offset:%zu not 4-byte aligned", offset);
    }
  }

  auto gl = static_cast<GLGPU*>(gpu)->functions();
  gl->bindBuffer(bufferTarget, _bufferID);
  _mappedRange = gl->mapBufferRange(
      bufferTarget, static_cast<int32_t>(offset), static_cast<int32_t>(size),
      GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
  if (_mappedRange != nullptr) {
    _mapState = GPUBufferMapState::MAPPED;
  }
  return _mappedRange;
}

void GLBuffer::unmap(GPU* gpu) {
  if (_mapState == GPUBufferMapState::UNMAPPED) {
    return;
  }

  if (gpu == nullptr) {
    return;
  }

  auto bufferTarget = target();
  auto gl = static_cast<GLGPU*>(gpu)->functions();
  gl->unmapBuffer(bufferTarget);
  gl->bindBuffer(bufferTarget, 0);
  _mapState = GPUBufferMapState::UNMAPPED;
  _mappedRange = nullptr;
}

void GLBuffer::release(GPU* gpu) {
  DEBUG_ASSERT(gpu != nullptr);
  if (_bufferID > 0) {
    auto gl = static_cast<const GLGPU*>(gpu)->functions();
    gl->deleteBuffers(1, &_bufferID);
    _bufferID = 0;
  }
}
}  // namespace tgfx
