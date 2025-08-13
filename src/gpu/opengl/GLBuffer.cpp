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
  LOGE("GLBuffer::target() invalid buffer usage!");
  return 0;
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
