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

#include "GLRenderPass.h"
#include "GLUtil.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProgramCache.h"
#include "gpu/opengl/GLProgram.h"

namespace tgfx {
struct AttribLayout {
  bool normalized = false;  // Only used by floating point types.
  int count = 0;
  unsigned type = 0;
};

static constexpr std::pair<SLType, AttribLayout> attribLayoutPair[] = {
    {SLType::Float, {false, 1, GL_FLOAT}},  {SLType::Float2, {false, 2, GL_FLOAT}},
    {SLType::Float3, {false, 3, GL_FLOAT}}, {SLType::Float4, {false, 4, GL_FLOAT}},
    {SLType::Int, {false, 1, GL_INT}},      {SLType::Int2, {false, 2, GL_INT}},
    {SLType::Int3, {false, 3, GL_INT}},     {SLType::Int4, {false, 4, GL_INT}}};

static AttribLayout GetAttribLayout(SLType type) {
  for (const auto& pair : attribLayoutPair) {
    if (pair.first == type) {
      return pair.second;
    }
  }
  return {false, 0, 0};
}

GLRenderPass::GLRenderPass(Context* context) : RenderPass(context) {
  if (GLCaps::Get(context)->vertexArrayObjectSupport) {
    vertexArray = GLVertexArray::Make(context);
    DEBUG_ASSERT(vertexArray != nullptr);
  }
  sharedVertexBuffer =
      std::static_pointer_cast<GLBuffer>(GpuBuffer::Make(context, BufferType::Vertex));
  DEBUG_ASSERT(sharedVertexBuffer != nullptr);
}

static void UpdateScissor(Context* context, const Rect& scissorRect) {
  auto gl = GLFunctions::Get(context);
  if (scissorRect.isEmpty()) {
    gl->disable(GL_SCISSOR_TEST);
  } else {
    gl->enable(GL_SCISSOR_TEST);
    gl->scissor(static_cast<int>(scissorRect.x()), static_cast<int>(scissorRect.y()),
                static_cast<int>(scissorRect.width()), static_cast<int>(scissorRect.height()));
  }
}

static const unsigned gXfermodeCoeff2Blend[] = {
    GL_ZERO,      GL_ONE,
    GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
};

static void UpdateBlend(Context* context, const BlendInfo* blendFactors) {
  auto gl = GLFunctions::Get(context);
  auto caps = GLCaps::Get(context);
  if (caps->frameBufferFetchSupport && caps->frameBufferFetchRequiresEnablePerSample) {
    if (blendFactors == nullptr) {
      gl->enable(GL_FETCH_PER_SAMPLE_ARM);
    } else {
      gl->disable(GL_FETCH_PER_SAMPLE_ARM);
    }
  }
  if (blendFactors == nullptr || (blendFactors->srcBlend == BlendModeCoeff::One &&
                                  blendFactors->dstBlend == BlendModeCoeff::Zero)) {
    // There is no need to enable blending if the blend mode is src.
    gl->disable(GL_BLEND);
  } else {
    gl->enable(GL_BLEND);
    gl->blendFunc(gXfermodeCoeff2Blend[static_cast<int>(blendFactors->srcBlend)],
                  gXfermodeCoeff2Blend[static_cast<int>(blendFactors->dstBlend)]);
    gl->blendEquation(GL_FUNC_ADD);
  }
}

void GLRenderPass::onBindRenderTarget() {
  auto gl = GLFunctions::Get(context);
  auto glRT = static_cast<GLRenderTarget*>(_renderTarget.get());
  gl->bindFramebuffer(GL_FRAMEBUFFER, glRT->getFrameBufferID());
  gl->viewport(0, 0, glRT->width(), glRT->height());
}

bool GLRenderPass::onBindProgramAndScissorClip(const ProgramInfo* programInfo,
                                               const Rect& scissorRect) {
  _program = static_cast<GLProgram*>(context->programCache()->getProgram(programInfo));
  if (_program == nullptr) {
    return false;
  }
  auto gl = GLFunctions::Get(context);
  CheckGLError(context);
  auto* program = static_cast<GLProgram*>(_program);
  gl->useProgram(program->programID());
  UpdateScissor(context, scissorRect);
  UpdateBlend(context, programInfo->blendInfo());
  if (programInfo->requiresBarrier()) {
    gl->textureBarrier();
  }
  program->updateUniformsAndTextureBindings(_renderTarget.get(), programInfo);
  return true;
}

static const unsigned gPrimitiveType[] = {GL_TRIANGLES, GL_TRIANGLE_STRIP};

void GLRenderPass::onDraw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount) {
  auto func = [&]() {
    auto gl = GLFunctions::Get(context);
    gl->drawArrays(gPrimitiveType[static_cast<int>(primitiveType)], static_cast<int>(baseVertex),
                   static_cast<int>(vertexCount));
  };
  draw(func);
}

void GLRenderPass::onDrawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount) {
  auto func = [&]() {
    auto gl = GLFunctions::Get(context);
    gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                   std::static_pointer_cast<GLBuffer>(_indexBuffer)->bufferID());
    gl->drawElements(gPrimitiveType[static_cast<int>(primitiveType)], static_cast<int>(indexCount),
                     GL_UNSIGNED_SHORT, reinterpret_cast<void*>(baseIndex * sizeof(uint16_t)));
    gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  };
  draw(func);
}

void GLRenderPass::draw(const std::function<void()>& func) {
  auto gl = GLFunctions::Get(context);
  if (vertexArray) {
    gl->bindVertexArray(vertexArray->id());
  }
  if (_vertexBuffer) {
    gl->bindBuffer(GL_ARRAY_BUFFER, std::static_pointer_cast<GLBuffer>(_vertexBuffer)->bufferID());
  } else {
    gl->bindBuffer(GL_ARRAY_BUFFER, sharedVertexBuffer->bufferID());
    gl->bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_vertexData->size()),
                   _vertexData->data(), GL_STATIC_DRAW);
  }
  auto* program = static_cast<GLProgram*>(_program);
  for (const auto& attribute : program->vertexAttributes()) {
    const AttribLayout& layout = GetAttribLayout(attribute.gpuType);
    gl->vertexAttribPointer(static_cast<unsigned>(attribute.location), layout.count, layout.type,
                            layout.normalized, program->vertexStride(),
                            reinterpret_cast<void*>(attribute.offset));
    gl->enableVertexAttribArray(static_cast<unsigned>(attribute.location));
  }
  func();
  if (vertexArray) {
    gl->bindVertexArray(0);
  }
  gl->bindBuffer(GL_ARRAY_BUFFER, 0);
  CheckGLError(context);
}

void GLRenderPass::onClear(const Rect& scissor, Color color) {
  auto gl = GLFunctions::Get(context);
  UpdateScissor(context, scissor);
  gl->clearColor(color.red, color.green, color.blue, color.alpha);
  gl->clear(GL_COLOR_BUFFER_BIT);
}

void GLRenderPass::onCopyTo(Texture* texture, const Rect& srcRect, const Point& dstPoint) {
  auto gl = GLFunctions::Get(context);
  auto glRT = static_cast<const GLRenderTarget*>(_renderTarget.get());
  auto hasMSAA = glRT->sampleCount() > 1;
  if (hasMSAA) {
    gl->bindFramebuffer(GL_FRAMEBUFFER, glRT->getFrameBufferID(false));
  }
  auto glSampler = static_cast<const GLSampler*>(texture->getSampler());
  gl->bindTexture(glSampler->target, glSampler->id);
  gl->copyTexSubImage2D(glSampler->target, 0, static_cast<int>(dstPoint.x),
                        static_cast<int>(dstPoint.y), static_cast<int>(srcRect.x()),
                        static_cast<int>(srcRect.y()), static_cast<int>(srcRect.width()),
                        static_cast<int>(srcRect.height()));
  if (glSampler->hasMipmaps() && glSampler->target == GL_TEXTURE_2D) {
    gl->generateMipmap(glSampler->target);
  }
  if (hasMSAA) {
    gl->bindFramebuffer(GL_FRAMEBUFFER, glRT->getFrameBufferID(true));
  }
}

}  // namespace tgfx
