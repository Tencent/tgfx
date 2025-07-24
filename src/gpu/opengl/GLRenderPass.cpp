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

#include "GLRenderPass.h"
#include "GLUtil.h"
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/opengl/GLProgram.h"
#include "gpu/opengl/GLRenderTarget.h"

namespace tgfx {
struct AttribLayout {
  bool normalized = false;  // Only used by floating point types.
  int count = 0;
  unsigned type = 0;
};

static constexpr std::pair<SLType, AttribLayout> attribLayoutPair[] = {
    {SLType::Float, {false, 1, GL_FLOAT}},
    {SLType::Float2, {false, 2, GL_FLOAT}},
    {SLType::Float3, {false, 3, GL_FLOAT}},
    {SLType::Float4, {false, 4, GL_FLOAT}},
    {SLType::Int, {false, 1, GL_INT}},
    {SLType::Int2, {false, 2, GL_INT}},
    {SLType::Int3, {false, 3, GL_INT}},
    {SLType::Int4, {false, 4, GL_INT}},
    {SLType::UByte4Color, {true, 4, GL_UNSIGNED_BYTE}}};

static AttribLayout GetAttribLayout(SLType type) {
  for (const auto& pair : attribLayoutPair) {
    if (pair.first == type) {
      return pair.second;
    }
  }
  return {false, 0, 0};
}

std::unique_ptr<RenderPass> RenderPass::Make(std::shared_ptr<RenderTarget> renderTarget) {
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto renderPass = std::make_unique<GLRenderPass>(std::move(renderTarget));
  renderPass->begin();
  return renderPass;
}

GLRenderPass::GLRenderPass(std::shared_ptr<RenderTarget> renderTarget)
    : RenderPass(std::move(renderTarget)) {
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
    GL_ZERO,       GL_ONE,
    GL_SRC_COLOR,  GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR,  GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA,  GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,  GL_ONE_MINUS_DST_ALPHA,
    GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR,
    GL_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA,
};

static const unsigned gXfermodeEquation2Blend[] = {
    GL_FUNC_ADD,
    GL_FUNC_SUBTRACT,
    GL_FUNC_REVERSE_SUBTRACT,
};

static void UpdateBlend(Context* context, const BlendFormula* blendFactors) {
  auto gl = GLFunctions::Get(context);
  auto caps = GLCaps::Get(context);
  if (caps->frameBufferFetchSupport && caps->frameBufferFetchRequiresEnablePerSample) {
    if (blendFactors == nullptr) {
      gl->enable(GL_FETCH_PER_SAMPLE_ARM);
    } else {
      gl->disable(GL_FETCH_PER_SAMPLE_ARM);
    }
  }
  if (blendFactors == nullptr || (blendFactors->srcCoeff() == BlendModeCoeff::One &&
                                  blendFactors->dstCoeff() == BlendModeCoeff::Zero &&
                                  (blendFactors->equation() == BlendEquation::Add ||
                                   blendFactors->equation() == BlendEquation::Subtract))) {
    // There is no need to enable blending if the blend mode is src.
    gl->disable(GL_BLEND);
  } else {
    gl->enable(GL_BLEND);
    gl->blendFunc(gXfermodeCoeff2Blend[static_cast<int>(blendFactors->srcCoeff())],
                  gXfermodeCoeff2Blend[static_cast<int>(blendFactors->dstCoeff())]);
    gl->blendEquation(gXfermodeEquation2Blend[static_cast<int>(blendFactors->equation())]);
  }
}

void GLRenderPass::begin() {
  auto context = getContext();
  auto gl = GLFunctions::Get(context);
  auto glRT = static_cast<GLRenderTarget*>(renderTarget.get());
  gl->bindFramebuffer(GL_FRAMEBUFFER, glRT->drawFrameBufferID());
  gl->viewport(0, 0, glRT->width(), glRT->height());
  if (auto vertexArray = GLContext::Unwrap(context)->sharedVertexArray()) {
    gl->bindVertexArray(vertexArray->id());
  }
}

void GLRenderPass::onEnd() {
  auto context = getContext();
  auto gl = GLFunctions::Get(context);
  if (GLContext::Unwrap(context)->sharedVertexArray()) {
    gl->bindVertexArray(0);
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GLRenderPass::onBindProgramAndScissorClip(const Pipeline* pipeline, const Rect& scissorRect) {
  auto context = getContext();
  program = context->globalCache()->getProgram(pipeline);
  if (program == nullptr) {
    return false;
  }
  ClearGLError(context);
  auto gl = GLFunctions::Get(context);
  auto glProgram = static_cast<GLProgram*>(program.get());
  gl->useProgram(glProgram->programID());
  UpdateScissor(context, scissorRect);
  UpdateBlend(context, pipeline->blendFormula());
  if (pipeline->requiresBarrier()) {
    gl->textureBarrier();
  }
  glProgram->updateUniformsAndTextureBindings(renderTarget.get(), pipeline);
  return true;
}

bool GLRenderPass::onBindBuffers(std::shared_ptr<GPUBuffer> indexBuffer,
                                 std::shared_ptr<GPUBuffer> vertexBuffer, size_t vertexOffset) {
  auto context = getContext();
  auto gl = GLFunctions::Get(context);
  if (vertexBuffer) {
    gl->bindBuffer(GL_ARRAY_BUFFER, std::static_pointer_cast<GLBuffer>(vertexBuffer)->bufferID());
  } else {
    return false;
  }
  auto glProgram = static_cast<GLProgram*>(program.get());
  for (const auto& attribute : glProgram->vertexAttributes()) {
    const AttribLayout& layout = GetAttribLayout(attribute.gpuType);
    auto offset = vertexOffset + attribute.offset;
    gl->vertexAttribPointer(static_cast<unsigned>(attribute.location), layout.count, layout.type,
                            layout.normalized, glProgram->vertexStride(),
                            reinterpret_cast<void*>(offset));
    gl->enableVertexAttribArray(static_cast<unsigned>(attribute.location));
  }
  if (indexBuffer) {
    gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                   std::static_pointer_cast<GLBuffer>(indexBuffer)->bufferID());
  }
  return true;
}

static const unsigned gPrimitiveType[] = {GL_TRIANGLES, GL_TRIANGLE_STRIP};

void GLRenderPass::onDraw(PrimitiveType primitiveType, size_t offset, size_t count,
                          bool drawIndexed) {
  auto gl = GLFunctions::Get(getContext());
  if (drawIndexed) {
    gl->drawElements(gPrimitiveType[static_cast<int>(primitiveType)], static_cast<int>(count),
                     GL_UNSIGNED_SHORT, reinterpret_cast<void*>(offset * sizeof(uint16_t)));
  } else {
    gl->drawArrays(gPrimitiveType[static_cast<int>(primitiveType)], static_cast<int>(offset),
                   static_cast<int>(count));
  }
}

void GLRenderPass::onClear(const Rect& scissor, Color color) {
  auto context = getContext();
  auto gl = GLFunctions::Get(context);
  UpdateScissor(context, scissor);
  gl->clearColor(color.red, color.green, color.blue, color.alpha);
  gl->clear(GL_COLOR_BUFFER_BIT);
}
}  // namespace tgfx
