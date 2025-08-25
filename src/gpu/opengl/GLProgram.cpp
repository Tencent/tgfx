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

#include "GLProgram.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
GLProgram::GLProgram(unsigned programID, std::unique_ptr<GLUniformBuffer> uniformBuffer,
                     std::vector<Attribute> attributes, int vertexStride)
    : programId(programID), _uniformBuffer(std::move(uniformBuffer)),
      attributes(std::move(attributes)), _vertexStride(vertexStride) {
}

void GLProgram::onReleaseGPU() {
  if (programId > 0) {
    auto gl = GLFunctions::Get(context);
    gl->deleteProgram(programId);
  }
}
}  // namespace tgfx
