/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLFrameBuffer.h"
#include "tgfx/gpu/opengl/GLFunctions.h"

namespace tgfx {
std::shared_ptr<GLFrameBuffer> GLFrameBuffer::Make(Context* context) {
  auto gl = GLFunctions::Get(context);
  unsigned id = 0;
  gl->genFramebuffers(1, &id);
  if (id == 0) {
    return nullptr;
  }
  return Resource::AddToCache(context, new GLFrameBuffer(id));
}

GLFrameBuffer::GLFrameBuffer(unsigned int id) : _id(id) {
}

void GLFrameBuffer::onReleaseGPU() {
  auto gl = GLFunctions::Get(context);
  if (_id > 0) {
    gl->deleteFramebuffers(1, &_id);
    _id = 0;
  }
}
}  // namespace tgfx
