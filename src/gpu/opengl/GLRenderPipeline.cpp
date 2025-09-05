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

#include "GLRenderPipeline.h"
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

struct AttribLayout {
  bool normalized = false;
  int count = 0;
  unsigned type = 0;
};

GLRenderPipeline::GLRenderPipeline(unsigned programID, std::vector<Uniform> uniforms,
                                   std::vector<Attribute> attribs,
                                   std::unique_ptr<BlendFormula> blendFormula)
    : programID(programID), uniforms(std::move(uniforms)), attributes(std::move(attribs)),
      blendFormula(std::move(blendFormula)) {
  DEBUG_ASSERT(!attributes.empty());
  size_t offset = 0;
  for (auto& attribute : attributes) {
    offset += attribute.size();
  }
  vertexStride = static_cast<int>(offset);
}

void GLRenderPipeline::activate(GLGPU* gpu) {
  auto gl = gpu->functions();
  auto caps = static_cast<const GLCaps*>(gpu->caps());
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
}

void GLRenderPipeline::setUniformBytes(GLGPU* gpu, const void* data, size_t size) {
  if (data == nullptr || size == 0) {
    return;
  }
  if (uniforms.empty()) {
    return;
  }
  auto gl = gpu->functions();
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
    }
    index++;
    offset += uniform.size();
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

void GLRenderPipeline::setVertexBuffer(GLGPU* gpu, GPUBuffer* vertexBuffer, size_t vertexOffset) {
  auto gl = gpu->functions();
  if (vertexBuffer == nullptr) {
    gl->bindBuffer(GL_ARRAY_BUFFER, 0);
    return;
  }
  gl->bindBuffer(GL_ARRAY_BUFFER, static_cast<GLBuffer*>(vertexBuffer)->bufferID());
  if (attributeLocations.empty()) {
    for (auto& attribute : attributes) {
      auto location = gl->getAttribLocation(programID, attribute.name().c_str());
      attributeLocations.push_back(location);
    }
  }
  size_t index = 0;
  size_t offset = vertexOffset;
  for (auto& attribute : attributes) {
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

void GLRenderPipeline::release(GPU* gpu) {
  DEBUG_ASSERT(gpu != nullptr);
  auto gl = static_cast<GLGPU*>(gpu)->functions();
  if (programID > 0) {
    gl->deleteProgram(programID);
  }
  if (vertexArray > 0) {
    gl->deleteVertexArrays(1, &vertexArray);
  }
}
}  // namespace tgfx
