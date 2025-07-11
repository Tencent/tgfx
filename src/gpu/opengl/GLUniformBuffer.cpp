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

#include "GLUniformBuffer.h"
#include "core/utils/Log.h"
#include "tgfx/gpu/opengl/GLFunctions.h"

namespace tgfx {
GLUniformBuffer::GLUniformBuffer(std::vector<Uniform> uniformList, std::vector<int> locationList)
    : UniformBuffer(std::move(uniformList)), locations(std::move(locationList)) {
  DEBUG_ASSERT(uniforms.size() == locations.size());
  if (!uniforms.empty()) {
    dirtyFlags.resize(uniforms.size(), true);
    size_t bufferSize = offsets.back() + uniforms.back().size();
    buffer = new (std::nothrow) uint8_t[bufferSize];
  }
}

GLUniformBuffer::~GLUniformBuffer() {
  delete[] buffer;
}

void GLUniformBuffer::onCopyData(size_t index, size_t offset, size_t size, const void* data) {
  if (!dirtyFlags[index] && memcmp(buffer + offset, data, size) == 0) {
    return;
  }
  dirtyFlags[index] = true;
  bufferChanged = true;
  memcpy(buffer + offset, data, size);
}

void GLUniformBuffer::uploadToGPU(Context* context) {
  if (!bufferChanged) {
    return;
  }
  bufferChanged = false;
  auto gl = GLFunctions::Get(context);
  size_t index = 0;
  for (auto& uniform : uniforms) {
    if (!dirtyFlags[index]) {
      index++;
      continue;
    }
    dirtyFlags[index] = false;
    auto location = locations[index];
    auto offset = offsets[index];
    switch (uniform.type) {
      case Uniform::Type::Float:
        gl->uniform1fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case Uniform::Type::Float2:
        gl->uniform2fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case Uniform::Type::Float3:
        gl->uniform3fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case Uniform::Type::Float4:
        gl->uniform4fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case Uniform::Type::Float2x2:
        gl->uniformMatrix2fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case Uniform::Type::Float3x3:
        gl->uniformMatrix3fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case Uniform::Type::Float4x4:
        gl->uniformMatrix4fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case Uniform::Type::Int:
        gl->uniform1iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case Uniform::Type::Int2:
        gl->uniform2iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case Uniform::Type::Int3:
        gl->uniform3iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case Uniform::Type::Int4:
        gl->uniform4iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
    }
    index++;
  }
}
}  // namespace tgfx
