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

namespace tgfx {
GLProgram::GLProgram(unsigned programID, std::vector<Uniform> uniforms, std::vector<int> locations,
                     std::vector<Attribute> attributes, int vertexStride)
    : Program(std::make_unique<UniformBuffer>(uniforms)), programId(programID),
      uniforms(std::move(uniforms)), uniformLocations(std::move(locations)),
      attributes(std::move(attributes)), _vertexStride(vertexStride) {
}

void GLProgram::onReleaseGPU() {
  if (programId > 0) {
    auto gl = GLFunctions::Get(context);
    gl->deleteProgram(programId);
  }
}

void GLProgram::setUniformBytes(const void* data, size_t size) {
  if (data == nullptr || size == 0) {
    return;
  }
  auto buffer = reinterpret_cast<uint8_t*>(const_cast<void*>(data));
  auto gl = GLFunctions::Get(context);
  size_t index = 0;
  size_t offset = 0;
  for (auto& uniform : uniforms) {
    auto location = uniformLocations[index];
    switch (uniform.type()) {
      case SLType::Float:
        gl->uniform1fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case SLType::Float2:
        gl->uniform2fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case SLType::Float3:
        gl->uniform3fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case SLType::Float4:
        gl->uniform4fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case SLType::Float2x2:
        gl->uniformMatrix2fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case SLType::Float3x3:
        gl->uniformMatrix3fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case SLType::Float4x4:
        gl->uniformMatrix4fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case SLType::Int:
        gl->uniform1iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case SLType::Int2:
        gl->uniform2iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case SLType::Int3:
        gl->uniform3iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case SLType::Int4:
        gl->uniform4iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      default:
        LOGE("GLProgram::updateUniforms() unsupported uniform type: %d",
             static_cast<int>(uniform.type()));
        break;
    }
    index++;
    offset += uniform.size();
  }
}
}  // namespace tgfx
