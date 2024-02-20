/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "GLContext.h"
#include "GLUtil.h"
#include "utils/UniqueID.h"

namespace tgfx {
static ScratchKey ComputeScratchKey(BufferType bufferType) {
  static const uint32_t Type = UniqueID::Next();
  ScratchKey scratchKey = {};
  scratchKey.write(Type);
  scratchKey.write(static_cast<uint32_t>(bufferType));
  return scratchKey;
}

std::shared_ptr<GpuBuffer> GpuBuffer::Make(Context* context, const void* buffer, size_t size,
                                           BufferType bufferType) {
  if (buffer == nullptr || size == 0) {
    return nullptr;
  }
  // 防止前面产生的GLError，导致后面CheckGLError逻辑返回错误结果
  CheckGLError(context);

  unsigned target = bufferType == BufferType::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
  auto scratchKey = ComputeScratchKey(bufferType);
  auto glBuffer = Resource::Find<GLBuffer>(context, scratchKey);
  auto gl = GLFunctions::Get(context);
  if (glBuffer == nullptr) {
    unsigned bufferID = 0;
    gl->genBuffers(1, &bufferID);
    if (bufferID == 0) {
      return nullptr;
    }
    glBuffer = Resource::AddToCache(context, new GLBuffer(bufferType, size, bufferID), scratchKey);
  } else {
    glBuffer->_sizeInBytes = size;
  }
  gl->bindBuffer(target, glBuffer->_bufferID);
  gl->bufferData(target, static_cast<GLsizeiptr>(size), buffer, GL_STATIC_DRAW);
  if (!CheckGLError(context)) {
    gl->bindBuffer(target, 0);
    return nullptr;
  }
  gl->bindBuffer(target, 0);
  return glBuffer;
}

void GLBuffer::onReleaseGPU() {
  if (_bufferID > 0) {
    auto gl = GLFunctions::Get(context);
    gl->deleteBuffers(1, &_bufferID);
    _bufferID = 0;
  }
}
}  // namespace tgfx
