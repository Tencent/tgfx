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
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
GLRenderPass::GLRenderPass(std::shared_ptr<GLInterface> interface,
                           std::shared_ptr<RenderTarget> renderTarget, bool resolveMSAA)
    : RenderPass(std::move(renderTarget)), interface(std::move(interface)),
      resolveMSAA(resolveMSAA) {
}

static void UpdateScissor(const GLFunctions* gl, const Rect& scissorRect) {
  if (scissorRect.isEmpty()) {
    gl->disable(GL_SCISSOR_TEST);
  } else {
    gl->enable(GL_SCISSOR_TEST);
    gl->scissor(static_cast<int>(scissorRect.x()), static_cast<int>(scissorRect.y()),
                static_cast<int>(scissorRect.width()), static_cast<int>(scissorRect.height()));
  }
}

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

static void UpdateBlend(const GLInterface* interface, const BlendFormula* blendFactors) {
  auto gl = interface->functions();
  auto caps = interface->caps();
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
    gl->blendFunc(XfermodeCoeff2Blend[static_cast<int>(blendFactors->srcCoeff())],
                  XfermodeCoeff2Blend[static_cast<int>(blendFactors->dstCoeff())]);
    gl->blendEquation(XfermodeEquation2Blend[static_cast<int>(blendFactors->equation())]);
  }
}

void GLRenderPass::begin() {
  auto gl = interface->functions();
  auto renderTexture = static_cast<GLTexture*>(renderTarget->getRenderTexture());
  gl->bindFramebuffer(GL_FRAMEBUFFER, renderTexture->frameBufferID());
  gl->viewport(0, 0, renderTarget->width(), renderTarget->height());
}

void GLRenderPass::onEnd() {
  auto gl = interface->functions();
  auto caps = interface->caps();
  if (resolveMSAA && renderTarget->sampleCount() > 1) {
    auto renderTexture = static_cast<GLTexture*>(renderTarget->getRenderTexture());
    auto sampleTexture = static_cast<GLTexture*>(renderTarget->getSampleTexture());
    gl->bindFramebuffer(GL_READ_FRAMEBUFFER, renderTexture->frameBufferID());
    gl->bindFramebuffer(GL_DRAW_FRAMEBUFFER, sampleTexture->frameBufferID());
    // MSAA resolve may be affected by the scissor test, so disable it here.
    gl->disable(GL_SCISSOR_TEST);
    if (caps->msFBOType == MSFBOType::ES_Apple) {
      gl->resolveMultisampleFramebuffer();
    } else {
      gl->blitFramebuffer(0, 0, renderTarget->width(), renderTarget->height(), 0, 0,
                          renderTarget->width(), renderTarget->height(), GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    }
  }
  if (caps->vertexArrayObjectSupport) {
    gl->bindVertexArray(0);
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GLRenderPass::onBindProgramAndScissorClip(const Pipeline* pipeline, const Rect& scissorRect) {
  auto context = renderTarget->getContext();
  program = context->globalCache()->getProgram(pipeline);
  if (program == nullptr) {
    return false;
  }
  auto gl = interface->functions();
  ClearGLError(gl);
  auto glProgram = static_cast<GLProgram*>(program.get());
  gl->useProgram(glProgram->programID());
  UpdateScissor(gl, scissorRect);
  UpdateBlend(interface.get(), glProgram->blendFormula());
  auto renderTexture = renderTarget->getRenderTexture();
  auto samplers = pipeline->getSamplers();
  int textureUnit = 0;
  bool requiresBarrier = false;
  for (auto& info : samplers) {
    if (info.texture == renderTexture) {
      requiresBarrier = true;
    }
    bindTexture(textureUnit++, info.texture, info.state);
  }
  if (requiresBarrier && interface->caps()->textureRedSupport) {
    gl->textureBarrier();
  }
  auto uniformBuffer = glProgram->uniformBuffer();
  pipeline->getUniforms(renderTarget.get(), uniformBuffer);
  glProgram->setUniformBytes(uniformBuffer->data(), uniformBuffer->size());
  return true;
}

bool GLRenderPass::onBindBuffers(GPUBuffer* indexBuffer, GPUBuffer* vertexBuffer,
                                 size_t vertexOffset) {
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto gl = interface->functions();
  auto glProgram = static_cast<GLProgram*>(program.get());
  glProgram->setVertexBuffer(static_cast<GLBuffer*>(vertexBuffer), vertexOffset);
  if (indexBuffer) {
    gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<const GLBuffer*>(indexBuffer)->bufferID());
  }
  return true;
}

static const unsigned gPrimitiveType[] = {GL_TRIANGLES, GL_TRIANGLE_STRIP};

void GLRenderPass::onDraw(PrimitiveType primitiveType, size_t offset, size_t count,
                          bool drawIndexed) {
  auto gl = interface->functions();
  if (drawIndexed) {
    gl->drawElements(gPrimitiveType[static_cast<int>(primitiveType)], static_cast<int>(count),
                     GL_UNSIGNED_SHORT, reinterpret_cast<void*>(offset * sizeof(uint16_t)));
  } else {
    gl->drawArrays(gPrimitiveType[static_cast<int>(primitiveType)], static_cast<int>(offset),
                   static_cast<int>(count));
  }
}

void GLRenderPass::onClear(const Rect& scissor, Color color) {
  auto gl = interface->functions();
  UpdateScissor(gl, scissor);
  gl->clearColor(color.red, color.green, color.blue, color.alpha);
  gl->clear(GL_COLOR_BUFFER_BIT);
}

static int FilterToGLMagFilter(FilterMode filterMode) {
  switch (filterMode) {
    case FilterMode::Nearest:
      return GL_NEAREST;
    case FilterMode::Linear:
      return GL_LINEAR;
  }
  return 0;
}

static int FilterToGLMinFilter(FilterMode filterMode, MipmapMode mipmapMode) {
  switch (mipmapMode) {
    case MipmapMode::None:
      return FilterToGLMagFilter(filterMode);
    case MipmapMode::Nearest:
      switch (filterMode) {
        case FilterMode::Nearest:
          return GL_NEAREST_MIPMAP_NEAREST;
        case FilterMode::Linear:
          return GL_LINEAR_MIPMAP_NEAREST;
      }
    case MipmapMode::Linear:
      switch (filterMode) {
        case FilterMode::Nearest:
          return GL_NEAREST_MIPMAP_LINEAR;
        case FilterMode::Linear:
          return GL_LINEAR_MIPMAP_LINEAR;
      }
  }
  return 0;
}

static int GetGLWrap(unsigned target, SamplerState::WrapMode wrapMode) {
  if (target == GL_TEXTURE_RECTANGLE) {
    if (wrapMode == SamplerState::WrapMode::ClampToBorder) {
      return GL_CLAMP_TO_BORDER;
    }
    return GL_CLAMP_TO_EDGE;
  }
  switch (wrapMode) {
    case SamplerState::WrapMode::Clamp:
      return GL_CLAMP_TO_EDGE;
    case SamplerState::WrapMode::Repeat:
      return GL_REPEAT;
    case SamplerState::WrapMode::MirrorRepeat:
      return GL_MIRRORED_REPEAT;
    case SamplerState::WrapMode::ClampToBorder:
      return GL_CLAMP_TO_BORDER;
  }
  return 0;
}

void GLRenderPass::bindTexture(int unitIndex, GPUTexture* texture, SamplerState samplerState) {
  if (texture == nullptr) {
    return;
  }
  auto gl = interface->functions();
  auto caps = interface->caps();
  auto glTexture = static_cast<const GLTexture*>(texture);
  auto target = glTexture->target();
  gl->activeTexture(static_cast<unsigned>(GL_TEXTURE0 + unitIndex));
  gl->bindTexture(target, glTexture->textureID());
  gl->texParameteri(target, GL_TEXTURE_WRAP_S, GetGLWrap(target, samplerState.wrapModeX));
  gl->texParameteri(target, GL_TEXTURE_WRAP_T, GetGLWrap(target, samplerState.wrapModeY));
  if (samplerState.mipmapped() && (!caps->mipmapSupport || glTexture->mipLevelCount() <= 1)) {
    samplerState.mipmapMode = MipmapMode::None;
  }
  gl->texParameteri(target, GL_TEXTURE_MIN_FILTER,
                    FilterToGLMinFilter(samplerState.filterMode, samplerState.mipmapMode));
  gl->texParameteri(target, GL_TEXTURE_MAG_FILTER, FilterToGLMagFilter(samplerState.filterMode));
}

}  // namespace tgfx
