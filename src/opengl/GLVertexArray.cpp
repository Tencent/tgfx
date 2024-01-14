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

#include "GLVertexArray.h"
#include "tgfx/opengl/GLFunctions.h"

namespace tgfx {
std::shared_ptr<GLVertexArray> GLVertexArray::Make(Context* context) {
  auto gl = GLFunctions::Get(context);
  unsigned id = 0;
  gl->genVertexArrays(1, &id);
  if (id == 0) {
    return nullptr;
  }
  return Resource::AddToCache(context, new GLVertexArray(id));
}

GLVertexArray::GLVertexArray(unsigned int id) : _id(id) {
}

void GLVertexArray::onReleaseGPU() {
  auto gl = GLFunctions::Get(context);
  if (_id > 0) {
    gl->deleteVertexArrays(1, &_id);
    _id = 0;
  }
}
}  // namespace tgfx
