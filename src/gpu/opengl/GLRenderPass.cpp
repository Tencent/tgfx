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
GLRenderPass::GLRenderPass(std::shared_ptr<GLInterface> interface, RenderPassDescriptor descriptor)
    : RenderPass(std::move(descriptor)), interface(std::move(interface)) {
}

void GLRenderPass::begin() {
  DEBUG_ASSERT(!descriptor.colorAttachments.empty());
  auto& colorAttachment = descriptor.colorAttachments[0];
  DEBUG_ASSERT(colorAttachment.texture != nullptr);
  auto renderTexture = static_cast<GLTexture*>(colorAttachment.texture);
  auto gl = interface->functions();
  gl->bindFramebuffer(GL_FRAMEBUFFER, renderTexture->frameBufferID());
  // Set the viewport to cover the entire color attachment by default.
  gl->viewport(0, 0, renderTexture->width(), renderTexture->height());
  // Disable scissor test by default.
  gl->disable(GL_SCISSOR_TEST);
  if (colorAttachment.loadAction == LoadAction::Clear) {
    auto& color = colorAttachment.clearValue;
    gl->clearColor(color.red, color.green, color.blue, color.alpha);
    gl->clear(GL_COLOR_BUFFER_BIT);
  }
}

void GLRenderPass::setScissorRect(int x, int y, int width, int height) {
  auto gl = interface->functions();
  auto texture = descriptor.colorAttachments[0].texture;
  if (x == 0 && y == 0 && width == texture->width() && height == texture->height()) {
    gl->disable(GL_SCISSOR_TEST);
  } else {
    gl->enable(GL_SCISSOR_TEST);
    gl->scissor(x, y, width, height);
  }
}

void GLRenderPass::onEnd() {
  auto gl = interface->functions();
  auto caps = interface->caps();
  auto& attachment = descriptor.colorAttachments[0];
  if (attachment.resolveTexture) {
    auto renderTexture = static_cast<GLTexture*>(attachment.texture);
    auto sampleTexture = static_cast<GLTexture*>(attachment.resolveTexture);
    DEBUG_ASSERT(renderTexture != sampleTexture);
    gl->bindFramebuffer(GL_READ_FRAMEBUFFER, renderTexture->frameBufferID());
    gl->bindFramebuffer(GL_DRAW_FRAMEBUFFER, sampleTexture->frameBufferID());
    // MSAA resolve may be affected by the scissor test, so disable it here.
    gl->disable(GL_SCISSOR_TEST);
    if (caps->msFBOType == MSFBOType::ES_Apple) {
      gl->resolveMultisampleFramebuffer();
    } else {
      gl->blitFramebuffer(0, 0, renderTexture->width(), renderTexture->height(), 0, 0,
                          sampleTexture->width(), sampleTexture->height(), GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    }
  }
  if (caps->vertexArrayObjectSupport) {
    gl->bindVertexArray(0);
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GLRenderPass::onBindProgram(const ProgramInfo* programInfo) {
  program = programInfo->getProgram();
  if (program == nullptr) {
    LOGE("GLRenderPass::onBindProgramAndScissorClip() Failed to get the program!");
    return false;
  }
  auto gl = interface->functions();
  ClearGLError(gl);
  auto glProgram = static_cast<GLProgram*>(program.get());
  glProgram->activate();
  auto renderTexture = descriptor.colorAttachments[0].texture;
  auto samplers = programInfo->getSamplers();
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
  programInfo->getUniforms(uniformBuffer);
  glProgram->setUniformBytes(uniformBuffer->data(), uniformBuffer->size());
  return true;
}

bool GLRenderPass::onBindBuffers(GPUBuffer* indexBuffer, GPUBuffer* vertexBuffer,
                                 size_t vertexOffset) {
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto glProgram = static_cast<GLProgram*>(program.get());
  glProgram->setVertexBuffer(vertexBuffer, vertexOffset);
  glProgram->setIndexBuffer(indexBuffer);
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
