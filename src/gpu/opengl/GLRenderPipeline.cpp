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
#include "gpu/UniformData.h"
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
  state->setEnabled(GL_CULL_FACE, cullFaceState != nullptr);
  if (cullFaceState) {
    state->setCullFaceState(*cullFaceState);
  }
}

void GLRenderPipeline::setUniformBuffer(GLGPU* gpu, unsigned binding, GLBuffer* buffer,
                                        size_t offset, size_t size) {
  DEBUG_ASSERT(gpu != nullptr);
  if (buffer == nullptr || size == 0) {
    return;
  }
  if (!(buffer->usage() & GPUBufferUsage::UNIFORM)) {
    LOGE("GLRenderPipeline::setUniformBuffer error, buffer usage is not UNIFORM!");
    return;
  }

  auto gl = gpu->functions();
  unsigned ubo = buffer->bufferID();
  if (ubo == 0) {
    LOGE("GLRenderPipeline::setUniformBuffer error, uniform buffer id is 0");
    return;
  }
  gl->bindBufferRange(GL_UNIFORM_BUFFER, binding, ubo, static_cast<int32_t>(offset),
                      static_cast<int32_t>(size));
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
  if (!(vertexBuffer->usage() & GPUBufferUsage::VERTEX)) {
    LOGE("GLRenderPipeline::setVertexBuffer error, buffer usage is not VERTEX!");
    return;
  }
  gl->bindBuffer(GL_ARRAY_BUFFER, vertexBuffer->bufferID());
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

void GLRenderPipeline::onRelease(GLGPU* gpu) {
  DEBUG_ASSERT(gpu != nullptr);
  auto gl = gpu->functions();
  if (programID > 0) {
    gl->deleteProgram(programID);
  }
  if (vertexArray > 0) {
    gl->deleteVertexArrays(1, &vertexArray);
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

static std::unique_ptr<GLCullFaceState> MakeCullFaceState(const CullFaceDescriptor& descriptor) {
  if (!descriptor.enabled) {
    return nullptr;
  }
  auto cullFaceState = std::make_unique<GLCullFaceState>();
  cullFaceState->frontFace = ToGLFrontFaceDirection(descriptor.frontDirection);
  cullFaceState->cullFace = ToGLCullFaceMode(descriptor.mode);
  return cullFaceState;
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
                                             const RenderPipelineDescriptor& descriptor) {
  auto gl = gpu->functions();
  ClearGLError(gl);
  auto state = gpu->state();
  state->useProgram(programID);
  gl->genVertexArrays(1, &vertexArray);
  if (vertexArray == 0) {
    LOGE("GLRenderPipeline::createVertexArrays: failed to create VAO!");
    return false;
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
  cullFaceState = MakeCullFaceState(descriptor.cullFace);

  for (auto& entry : descriptor.layout.uniformBlocks) {
    auto uniformBlockIndex = gl->getUniformBlockIndex(programID, entry.name.c_str());
    if (uniformBlockIndex != GL_INVALID_INDEX) {
      gl->uniformBlockBinding(programID, uniformBlockIndex, entry.binding);
    }
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
