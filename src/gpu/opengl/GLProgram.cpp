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

static const unsigned XfermodeCoeff2Blend[] = {
    GL_ZERO,       GL_ONE,
    GL_SRC_COLOR,  GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR,  GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA,  GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,  GL_ONE_MINUS_DST_ALPHA,
    GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR,
    GL_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA,
};

static const unsigned XfermodeEquation2Blend[] = {
    GL_FUNC_ADD,
    GL_FUNC_SUBTRACT,
    GL_FUNC_REVERSE_SUBTRACT,
};

static constexpr int VERTEX_UBO_BINDING_POINT = 0;
static constexpr int FRAGMENT_UBO_BINDING_POINT = 1;

struct AttribLayout {
  bool normalized = false;
  int count = 0;
  unsigned type = 0;
};

GLProgram::GLProgram(unsigned programID, std::unique_ptr<UniformBuffer> uniformBuffer,
                     std::vector<Attribute> attributes, std::unique_ptr<BlendFormula> blendFormula)
    : Program(std::move(uniformBuffer)), programID(programID), _attributes(std::move(attributes)),
      blendFormula(std::move(blendFormula)) {
  DEBUG_ASSERT(!_attributes.empty());
  size_t offset = 0;
  for (auto& attribute : _attributes) {
    offset += attribute.size();
  }
  vertexStride = static_cast<int>(offset);
}

void GLProgram::activate() {
  auto gl = GLFunctions::Get(context);
  auto caps = GLCaps::Get(context);
  gl->useProgram(programID);
  if (caps->frameBufferFetchSupport && caps->frameBufferFetchRequiresEnablePerSample) {
    if (blendFormula == nullptr) {
      gl->enable(GL_FETCH_PER_SAMPLE_ARM);
    } else {
      gl->disable(GL_FETCH_PER_SAMPLE_ARM);
    }
  }
  if (blendFormula == nullptr || (blendFormula->srcCoeff() == BlendModeCoeff::One &&
                                  blendFormula->dstCoeff() == BlendModeCoeff::Zero &&
                                  (blendFormula->equation() == BlendEquation::Add ||
                                   blendFormula->equation() == BlendEquation::Subtract))) {
    // There is no need to enable blending if the blend mode is src.
    gl->disable(GL_BLEND);
  } else {
    gl->enable(GL_BLEND);
    gl->blendFunc(XfermodeCoeff2Blend[static_cast<int>(blendFormula->srcCoeff())],
                  XfermodeCoeff2Blend[static_cast<int>(blendFormula->dstCoeff())]);
    gl->blendEquation(XfermodeEquation2Blend[static_cast<int>(blendFormula->equation())]);
  }
  if (caps->vertexArrayObjectSupport) {
    if (vertexArray == 0) {
      gl->genVertexArrays(1, &vertexArray);
    }
    if (vertexArray > 0) {
      gl->bindVertexArray(vertexArray);
    }
  }

  auto setupUBO = [&](unsigned& ubo, const size_t bufferSize) -> bool {
    if (ubo == 0) {
      gl->genBuffers(1, &ubo);
    }
    if (ubo <= 0) {
      return false;
    }
    gl->bindBuffer(GL_UNIFORM_BUFFER, ubo);
    gl->bufferData(GL_UNIFORM_BUFFER, static_cast<int64_t>(bufferSize), nullptr, GL_STATIC_DRAW);
    return true;
  };

  if (caps->uboSupport) {
    if (setupUBO(vertexUBO, uniformBuffer()->vertexUniformBufferSize())) {
      vertexUniformBlockIndex = gl->getUniformBlockIndex(programID, VertexUniformBlockName.c_str());
      if (vertexUniformBlockIndex != GL_INVALID_INDEX) {
        gl->uniformBlockBinding(programID, vertexUniformBlockIndex, VERTEX_UBO_BINDING_POINT);
        gl->bindBufferBase(GL_UNIFORM_BUFFER, VERTEX_UBO_BINDING_POINT, vertexUBO);
      }
    }
    if (setupUBO(fragmentUBO, uniformBuffer()->fragmentUniformBufferSize())) {
      fragmentUniformBlockIndex = gl->getUniformBlockIndex(programID, FragmentUniformBlockName.c_str());
      if (fragmentUniformBlockIndex != GL_INVALID_INDEX) {
        gl->uniformBlockBinding(programID, fragmentUniformBlockIndex, FRAGMENT_UBO_BINDING_POINT);
        gl->bindBufferBase(GL_UNIFORM_BUFFER, FRAGMENT_UBO_BINDING_POINT, fragmentUBO);
      }
    }
    gl->bindBuffer(GL_UNIFORM_BUFFER, 0);
  }
}

void GLProgram::setUniformBytes() {
  DEBUG_ASSERT(_uniformBuffer != nullptr);
  const auto* data = _uniformBuffer->data();
  const auto size = _uniformBuffer->size();
  if (data == nullptr || size == 0) {
    return;
  }

  auto& uniforms = _uniformBuffer->uniforms();
  if (uniforms.empty()) {
    return;
  }
  auto gl = GLFunctions::Get(context);
  if (uniformLocations.empty()) {
    uniformLocations.reserve(uniforms.size());
    for (auto& uniform : uniforms) {
      auto location = gl->getUniformLocation(programID, uniform.name().c_str());
      uniformLocations.push_back(location);
    }
  }

  size_t index = 0;
  size_t offset = 0;
  auto buffer = reinterpret_cast<uint8_t*>(const_cast<void*>(data));
  for (auto& uniform : uniforms) {
    auto location = uniformLocations[index];
    if (location < 0) {
      continue;
    }
    switch (uniform.format()) {
      case UniformFormat::Float:
        gl->uniform1fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case UniformFormat::Float2:
        gl->uniform2fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case UniformFormat::Float3:
        gl->uniform3fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case UniformFormat::Float4:
        gl->uniform4fv(location, 1, reinterpret_cast<float*>(buffer + offset));
        break;
      case UniformFormat::Float2x2:
        gl->uniformMatrix2fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case UniformFormat::Float3x3:
        gl->uniformMatrix3fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case UniformFormat::Float4x4:
        gl->uniformMatrix4fv(location, 1, GL_FALSE, reinterpret_cast<float*>(buffer + offset));
        break;
      case UniformFormat::Int:
        gl->uniform1iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case UniformFormat::Int2:
        gl->uniform2iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case UniformFormat::Int3:
        gl->uniform3iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case UniformFormat::Int4:
        gl->uniform4iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
      case UniformFormat::Texture2DSampler:
      case UniformFormat::TextureExternalSampler:
      case UniformFormat::Texture2DRectSampler:
        gl->uniform1iv(location, 1, reinterpret_cast<int*>(buffer + offset));
        break;
    }
    index++;
    offset += uniform.size();
  }
}

void GLProgram::setUniformBuffer() const {
  DEBUG_ASSERT(_uniformBuffer != nullptr);
  DEBUG_ASSERT(vertexUBO > 0);
  DEBUG_ASSERT(fragmentUBO > 0);

  const auto* vertexData = _uniformBuffer->vertexUniformBufferData();
  const auto vertexSize = static_cast<int>(_uniformBuffer->vertexUniformBufferSize());
  const auto* fragmentData = _uniformBuffer->fragmentUniformBufferData();
  const auto fragmentSize = static_cast<int>(_uniformBuffer->fragmentUniformBufferSize());

  const auto* gl = GLFunctions::Get(context);
  if (vertexData != nullptr && vertexSize > 0) {
    gl->bindBuffer(GL_UNIFORM_BUFFER, vertexUBO);
    gl->bufferSubData(GL_UNIFORM_BUFFER, 0, vertexSize, vertexData);
    gl->bindBuffer(GL_UNIFORM_BUFFER, 0);
  }
  if (fragmentData != nullptr && fragmentSize > 0) {
    gl->bindBuffer(GL_UNIFORM_BUFFER, fragmentUBO);
    gl->bufferSubData(GL_UNIFORM_BUFFER, 0, fragmentSize, fragmentData);
    gl->bindBuffer(GL_UNIFORM_BUFFER, 0);
  }
}

static AttribLayout GetAttribLayout(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:
      return {false, 1, GL_FLOAT};
    case VertexFormat::Float2:
      return {false, 2, GL_FLOAT};
    case VertexFormat::Float3:
      return {false, 3, GL_FLOAT};
    case VertexFormat::Float4:
      return {false, 4, GL_FLOAT};
    case VertexFormat::Half:
      return {false, 1, GL_HALF_FLOAT};
    case VertexFormat::Half2:
      return {false, 2, GL_HALF_FLOAT};
    case VertexFormat::Half3:
      return {false, 3, GL_HALF_FLOAT};
    case VertexFormat::Half4:
      return {false, 4, GL_HALF_FLOAT};
    case VertexFormat::Int:
      return {false, 1, GL_INT};
    case VertexFormat::Int2:
      return {false, 2, GL_INT};
    case VertexFormat::Int3:
      return {false, 3, GL_INT};
    case VertexFormat::Int4:
      return {false, 4, GL_INT};
    case VertexFormat::UByteNormalized:
      return {true, 1, GL_UNSIGNED_BYTE};
    case VertexFormat::UByte2Normalized:
      return {true, 2, GL_UNSIGNED_BYTE};
    case VertexFormat::UByte3Normalized:
      return {true, 3, GL_UNSIGNED_BYTE};
    case VertexFormat::UByte4Normalized:
      return {true, 4, GL_UNSIGNED_BYTE};
  }
  return {false, 0, 0};
}

void GLProgram::setVertexBuffer(GPUBuffer* vertexBuffer, size_t vertexOffset) {
  if (vertexBuffer == nullptr) {
    return;
  }
  auto gl = GLFunctions::Get(context);
  gl->bindBuffer(GL_ARRAY_BUFFER, static_cast<GLBuffer*>(vertexBuffer)->bufferID());
  if (attributeLocations.empty()) {
    for (auto& attribute : _attributes) {
      auto location = gl->getAttribLocation(programID, attribute.name().c_str());
      attributeLocations.push_back(location);
    }
  }
  size_t index = 0;
  size_t offset = vertexOffset;
  for (auto& attribute : _attributes) {
    auto location = attributeLocations[index++];
    if (location >= 0) {
      auto layout = GetAttribLayout(attribute.format());
      gl->vertexAttribPointer(static_cast<unsigned>(location), layout.count, layout.type,
                              layout.normalized, vertexStride, reinterpret_cast<void*>(offset));
      gl->enableVertexAttribArray(static_cast<unsigned>(location));
    }
    offset += attribute.size();
  }
}

void GLProgram::setIndexBuffer(GPUBuffer* indexBuffer) {
  if (indexBuffer == nullptr) {
    return;
  }
  auto gl = GLFunctions::Get(context);
  gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLBuffer*>(indexBuffer)->bufferID());
}

void GLProgram::onReleaseGPU() {
  auto gl = GLFunctions::Get(context);
  if (programID > 0) {
    gl->deleteProgram(programID);
  }

  if (vertexArray > 0) {
    gl->deleteVertexArrays(1, &vertexArray);
  }

  if (vertexUBO > 0) {
    gl->deleteBuffers(1, &vertexUBO);
    vertexUBO = 0;
  }
  if (fragmentUBO > 0) {
    gl->deleteBuffers(1, &fragmentUBO);
    fragmentUBO = 0;
  }
}
}  // namespace tgfx
