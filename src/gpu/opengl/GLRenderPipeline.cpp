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
#include "gpu/UniformBuffer.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLRenderPipeline::GLRenderPipeline(unsigned programID) : programID(programID) {
}

void GLRenderPipeline::activate(GLGPU* gpu, bool depthReadOnly, bool stencilReadOnly,
                                unsigned stencilReference) {
  auto state = gpu->state();
  state->useProgram(programID);
  auto shaderCaps = gpu->caps()->shaderCaps();
  if (shaderCaps->frameBufferFetchSupport && shaderCaps->frameBufferFetchRequiresEnablePerSample) {
    if (blendState) {
      state->setEnabled(GL_FETCH_PER_SAMPLE_ARM, false);
    } else {
      state->setEnabled(GL_FETCH_PER_SAMPLE_ARM, true);
    }
  }
  state->setColorMask(colorWriteMask);

  state->setEnabled(GL_STENCIL_TEST, stencilState != nullptr);
  if (stencilState) {
    auto stencil = *stencilState;
    stencil.reference = stencilReference;
    if (stencilReadOnly) {
      stencil.writeMask = 0;
    }
    state->setStencilState(stencil);
  }
  state->setEnabled(GL_DEPTH_TEST, depthState != nullptr);
  if (depthState) {
    auto depth = *depthState;
    if (depthReadOnly) {
      depth.writeMask = 0;
    }
    state->setDepthState(depth);
  }
  state->setEnabled(GL_BLEND, blendState != nullptr);
  if (blendState) {
    state->setBlendState(*blendState);
  }
  if (vertexArray > 0) {
    state->bindVertexArray(vertexArray);
  }
}

void GLRenderPipeline::setUniformBytes(GLGPU* gpu, unsigned binding, const void* data,
                                       size_t size) {
  if (data == nullptr || size == 0) {
    return;
  }
  auto result = uniformBlocks.find(binding);
  if (result == uniformBlocks.end()) {
    LOGE("GLRenderPipeline::setUniformBytesForUBO: binding %d not found", binding);
    return;
  }
  auto& block = result->second;
  auto gl = gpu->functions();
  if (block.ubo > 0) {
    gl->bindBuffer(GL_UNIFORM_BUFFER, block.ubo);
    gl->bufferData(GL_UNIFORM_BUFFER, static_cast<int32_t>(size), data, GL_STATIC_DRAW);
    gl->bindBufferBase(GL_UNIFORM_BUFFER, binding, block.ubo);
    return;
  }
  auto buffer = reinterpret_cast<uint8_t*>(const_cast<void*>(data));
  for (auto& uniform : block.uniforms) {
    auto uniformData = buffer + uniform.offset;
    switch (uniform.format) {
      case UniformFormat::Float:
        gl->uniform1fv(uniform.location, 1, reinterpret_cast<float*>(uniformData));
        break;
      case UniformFormat::Float2:
        gl->uniform2fv(uniform.location, 1, reinterpret_cast<float*>(uniformData));
        break;
      case UniformFormat::Float3:
        gl->uniform3fv(uniform.location, 1, reinterpret_cast<float*>(uniformData));
        break;
      case UniformFormat::Float4:
        gl->uniform4fv(uniform.location, 1, reinterpret_cast<float*>(uniformData));
        break;
      case UniformFormat::Float2x2:
        gl->uniformMatrix2fv(uniform.location, 1, GL_FALSE, reinterpret_cast<float*>(uniformData));
        break;
      case UniformFormat::Float3x3:
        gl->uniformMatrix3fv(uniform.location, 1, GL_FALSE, reinterpret_cast<float*>(uniformData));
        break;
      case UniformFormat::Float4x4:
        gl->uniformMatrix4fv(uniform.location, 1, GL_FALSE, reinterpret_cast<float*>(uniformData));
        break;
      case UniformFormat::Int:
        gl->uniform1iv(uniform.location, 1, reinterpret_cast<int*>(uniformData));
        break;
      case UniformFormat::Int2:
        gl->uniform2iv(uniform.location, 1, reinterpret_cast<int*>(uniformData));
        break;
      case UniformFormat::Int3:
        gl->uniform3iv(uniform.location, 1, reinterpret_cast<int*>(uniformData));
        break;
      case UniformFormat::Int4:
        gl->uniform4iv(uniform.location, 1, reinterpret_cast<int*>(uniformData));
        break;
      case UniformFormat::Texture2DSampler:
      case UniformFormat::TextureExternalSampler:
      case UniformFormat::Texture2DRectSampler:
        gl->uniform1iv(uniform.location, 1, reinterpret_cast<int*>(uniformData));
        break;
    }
  }
}

void GLRenderPipeline::setTexture(GLGPU* gpu, unsigned binding, GLTexture* texture,
                                  GLSampler* sampler) {
  DEBUG_ASSERT(texture != nullptr);
  DEBUG_ASSERT(sampler != nullptr);
  auto result = textureUnits.find(binding);
  if (result == textureUnits.end()) {
    LOGE("GLRenderPipeline::setTexture: binding %d not found", binding);
    return;
  }
  auto state = gpu->state();
  state->bindTexture(texture, result->second);
  texture->updateSampler(gpu, sampler);
}

void GLRenderPipeline::setVertexBuffer(GLGPU* gpu, GLBuffer* vertexBuffer, size_t vertexOffset) {
  auto gl = gpu->functions();
  if (vertexBuffer == nullptr) {
    gl->bindBuffer(GL_ARRAY_BUFFER, 0);
    return;
  }
  gl->bindBuffer(GL_ARRAY_BUFFER, static_cast<GLBuffer*>(vertexBuffer)->bufferID());
  for (auto& attribute : attributes) {
    gl->vertexAttribPointer(static_cast<unsigned>(attribute.location), attribute.count,
                            attribute.type, attribute.normalized, static_cast<int>(vertexStride),
                            reinterpret_cast<void*>(attribute.offset + vertexOffset));
    gl->enableVertexAttribArray(static_cast<unsigned>(attribute.location));
  }
}

void GLRenderPipeline::setStencilReference(GLGPU* gpu, unsigned reference) {
  if (stencilState == nullptr) {
    return;
  }
  stencilState->reference = reference;
  auto state = gpu->state();
  state->setStencilState(*stencilState);
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
  for (auto& item : uniformBlocks) {
    auto& uniformBlock = item.second;
    if (uniformBlock.ubo > 0) {
      gl->deleteBuffers(1, &uniformBlock.ubo);
      uniformBlock.ubo = 0;
    }
  }
}

static GLAttribute MakeGLAttribute(VertexFormat format, int location, size_t offset) {
  switch (format) {
    case VertexFormat::Float:
      return {location, 1, GL_FLOAT, false, offset};
    case VertexFormat::Float2:
      return {location, 2, GL_FLOAT, false, offset};
    case VertexFormat::Float3:
      return {location, 3, GL_FLOAT, false, offset};
    case VertexFormat::Float4:
      return {location, 4, GL_FLOAT, false, offset};
    case VertexFormat::Half:
      return {location, 1, GL_HALF_FLOAT, false, offset};
    case VertexFormat::Half2:
      return {location, 2, GL_HALF_FLOAT, false, offset};
    case VertexFormat::Half3:
      return {location, 3, GL_HALF_FLOAT, false, offset};
    case VertexFormat::Half4:
      return {location, 4, GL_HALF_FLOAT, false, offset};
    case VertexFormat::Int:
      return {location, 1, GL_INT, false, offset};
    case VertexFormat::Int2:
      return {location, 2, GL_INT, false, offset};
    case VertexFormat::Int3:
      return {location, 3, GL_INT, false, offset};
    case VertexFormat::Int4:
      return {location, 4, GL_INT, false, offset};
    case VertexFormat::UByteNormalized:
      return {location, 1, GL_UNSIGNED_BYTE, true, offset};
    case VertexFormat::UByte2Normalized:
      return {location, 2, GL_UNSIGNED_BYTE, true, offset};
    case VertexFormat::UByte3Normalized:
      return {location, 3, GL_UNSIGNED_BYTE, true, offset};
    case VertexFormat::UByte4Normalized:
      return {location, 4, GL_UNSIGNED_BYTE, true, offset};
  }
  return {location, 0, 0, false, offset};
}

static std::unique_ptr<GLBlendState> MakeBlendState(const PipelineColorAttachment& attachment) {
  if (!attachment.blendEnable) {
    return nullptr;
  }
  auto blendState = std::make_unique<GLBlendState>();
  blendState->srcColorFactor = ToGLBlendFactor(attachment.srcColorBlendFactor);
  blendState->dstColorFactor = ToGLBlendFactor(attachment.dstColorBlendFactor);
  blendState->srcAlphaFactor = ToGLBlendFactor(attachment.srcAlphaBlendFactor);
  blendState->dstAlphaFactor = ToGLBlendFactor(attachment.dstAlphaBlendFactor);
  blendState->colorOp = ToGLBlendOperation(attachment.colorBlendOp);
  blendState->alphaOp = ToGLBlendOperation(attachment.alphaBlendOp);
  return blendState;
}

static GLStencil MakeGLStencil(const StencilDescriptor& descriptor) {
  GLStencil stencil = {};
  stencil.compare = ToGLCompareFunction(descriptor.compare);
  stencil.failOp = ToGLStencilOperation(descriptor.failOp);
  stencil.depthFailOp = ToGLStencilOperation(descriptor.depthFailOp);
  stencil.passOp = ToGLStencilOperation(descriptor.passOp);
  return stencil;
}

static std::unique_ptr<GLStencilState> MakeStencilState(const DepthStencilDescriptor& descriptor) {
  auto& stencilFront = descriptor.stencilFront;
  auto& stencilBack = descriptor.stencilBack;
  if (stencilFront.compare == CompareFunction::Always &&
      stencilBack.compare == CompareFunction::Always) {
    return nullptr;
  }
  auto stencilState = std::make_unique<GLStencilState>();
  stencilState->front = MakeGLStencil(descriptor.stencilFront);
  stencilState->back = MakeGLStencil(descriptor.stencilBack);
  stencilState->readMask = descriptor.stencilReadMask;
  stencilState->writeMask = descriptor.stencilWriteMask;
  return stencilState;
}

static std::unique_ptr<GLDepthState> MakeDepthState(const DepthStencilDescriptor& descriptor) {
  if (descriptor.depthCompare == CompareFunction::Always) {
    return nullptr;
  }
  auto depthState = std::make_unique<GLDepthState>();
  depthState->compare = ToGLCompareFunction(descriptor.depthCompare);
  depthState->writeMask = descriptor.depthWriteEnabled;
  return depthState;
}

bool GLRenderPipeline::setPipelineDescriptor(GLGPU* gpu,
                                             const GPURenderPipelineDescriptor& descriptor) {
  auto gl = gpu->functions();
  ClearGLError(gl);
  auto state = gpu->state();
  state->useProgram(programID);
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  if (caps->vertexArrayObjectSupport) {
    gl->genVertexArrays(1, &vertexArray);
    if (vertexArray == 0) {
      LOGE("GLRenderPipeline::createVertexArrays: failed to create VAO!");
      return false;
    }
  }

  DEBUG_ASSERT(!descriptor.vertex.attributes.empty());
  DEBUG_ASSERT(descriptor.vertex.vertexStride > 0);
  size_t vertexOffset = 0;
  attributes.reserve(descriptor.vertex.attributes.size());
  for (const auto& attribute : descriptor.vertex.attributes) {
    auto location = gl->getAttribLocation(programID, attribute.name().c_str());
    if (location != -1) {
      attributes.push_back(MakeGLAttribute(attribute.format(), location, vertexOffset));
    }
    vertexOffset += attribute.size();
  }
  vertexStride = descriptor.vertex.vertexStride;

  DEBUG_ASSERT(descriptor.fragment.colorAttachments.size() == 1);
  auto& attachment = descriptor.fragment.colorAttachments[0];
  colorWriteMask = attachment.colorWriteMask;
  stencilState = MakeStencilState(descriptor.depthStencil);
  depthState = MakeDepthState(descriptor.depthStencil);
  blendState = MakeBlendState(attachment);

  for (auto& entry : descriptor.layout.uniformBlocks) {
    GLUniformBlock block = {};
    if (entry.uniforms.empty()) {
      DEBUG_ASSERT(gpu->caps()->shaderCaps()->uboSupport);
      gl->genBuffers(1, &block.ubo);
      if (block.ubo == 0) {
        LOGE("GLRenderPipeline::createUniformBlocks: failed to create UBO!");
        return false;
      }
      auto index = gl->getUniformBlockIndex(programID, entry.name.c_str());
      if (index != GL_INVALID_INDEX) {
        gl->uniformBlockBinding(programID, index, entry.binding);
      }
    } else {
      block.uniforms.reserve(entry.uniforms.size());
      size_t uniformOffset = 0;
      for (auto& uniform : entry.uniforms) {
        auto location = gl->getUniformLocation(programID, uniform.name().c_str());
        if (location != -1) {
          block.uniforms.push_back({uniform.format(), location, uniformOffset});
        }
        uniformOffset += uniform.size();
      }
    }
    uniformBlocks[entry.binding] = block;
  }

  // Assign texture units to sampler uniforms up front, just once.
  unsigned textureUint = 0;
  for (auto& entry : descriptor.layout.textureSamplers) {
    auto location = gl->getUniformLocation(programID, entry.name.c_str());
    if (location == -1) {
      continue;
    }
    textureUnits[entry.binding] = textureUint;
    gl->uniform1i(location, static_cast<int>(textureUint));
    textureUint++;
  }

  return CheckGLError(gl);
}
}  // namespace tgfx
