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
WebGLBuffer::WebGLBuffer(unsigned bufferID, size_t size, uint32_t usage)
    : GLBuffer(bufferID, size, usage), uniqueID(UniqueID::Next()), _bufferID(bufferID) {
}

WebGLBuffer::~WebGLBuffer() {
  releaseInternal();
}

void* WebGLBuffer::getMappedRange(GPU* gpu, size_t offset, size_t size) {
  if (_mapState == GPUBufferMapStata::MAPPED) {
    return _mappedRange;
  }

  if (gpu == nullptr) {
    return nullptr;
  }

  if (offset + size > _size) {
    LOGE("WebGLBuffer::getMappedRange() out of range!");
    return nullptr;
  }

  auto bufferTarget = target();
  if (bufferTarget == GL_UNIFORM_BUFFER) {
    auto uboOffsetAlignment = static_cast<size_t>(gpu->caps()->shaderCaps()->uboOffsetAlignment);
    if (uboOffsetAlignment <= 0 || offset % uboOffsetAlignment != 0) {
      LOGE("WebGLBuffer::getMappedRange() invalid UBO offset:%zu, must be aligned to %zu bytes",
           offset, uboOffsetAlignment);
      return nullptr;
    }
  } else if (bufferTarget == GL_ELEMENT_ARRAY_BUFFER) {
    if (offset % 4 != 0) {
      LOGE("WebGLBuffer::getMappedRange() EBO offset:%zu not 4-byte aligned", offset);
    }
  }

  auto gl = static_cast<GLGPU*>(gpu)->functions();
  gl->bindBuffer(bufferTarget, _bufferID);
  _mappedRange = malloc(size);
  _mapState = GPUBufferMapStata::MAPPED;
  _mappedOffset = offset;
  _mappedSize = size;

  return _mappedRange;
}

void WebGLBuffer::unmap(GPU* gpu) {
  if (_mapState == GPUBufferMapStata::UNMAPPED || _mappedRange == nullptr) {
    return;
  }

  if (gpu == nullptr) {
    return;
  }

  auto bufferTarget = target();
  auto gl = static_cast<GLGPU*>(gpu)->functions();

  gl->bufferSubData(bufferTarget, static_cast<int32_t>(_mappedOffset),
                    static_cast<int32_t>(_mappedSize), _mappedRange);
  gl->bindBuffer(bufferTarget, 0);

  releaseInternal();
}

void WebGLBuffer::release(GPU* gpu) {
  GLBuffer::release(gpu);

  releaseInternal();
}

void WebGLBuffer::releaseInternal() {
  if (_mappedRange != nullptr) {
    free(_mappedRange);
  }
  _mappedRange = nullptr;
  _mapState = GPUBufferMapStata::UNMAPPED;
}
}  // namespace tgfx
