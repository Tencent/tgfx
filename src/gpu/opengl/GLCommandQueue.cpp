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

#include "GLCommandQueue.h"
#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
bool GLCommandQueue::writeBuffer(GPUBuffer* buffer, size_t bufferOffset, const void* data,
                                 size_t size) {
  if (data == nullptr || size == 0) {
    LOGE("GLCommandQueue::writeBuffer() data is null or size is zero!");
    return false;
  }
  if (bufferOffset + size > buffer->size()) {
    LOGE("GLCommandQueue::writeBuffer() size exceeds buffer size!");
    return false;
  }
  auto gl = interface->functions();
  ClearGLError(gl);
  auto glBuffer = static_cast<const GLBuffer*>(buffer);
  auto target = glBuffer->target();
  gl->bindBuffer(target, glBuffer->bufferID());
  gl->bufferSubData(target, static_cast<GLintptr>(bufferOffset), static_cast<GLsizeiptr>(size),
                    data);
  gl->bindBuffer(target, 0);
  return CheckGLError(gl);
}

void GLCommandQueue::submit(std::shared_ptr<CommandBuffer>) {
  auto gl = interface->functions();
  gl->flush();
}

void GLCommandQueue::waitUntilCompleted() {
  auto gl = interface->functions();
  gl->finish();
}

}  // namespace tgfx
