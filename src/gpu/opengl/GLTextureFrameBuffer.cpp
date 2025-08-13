/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GLTextureFrameBuffer.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLTexture.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
static bool RenderbufferStorageMSAA(GLGPU* gpu, int sampleCount, PixelFormat pixelFormat, int width,
                                    int height) {
  auto gl = gpu->functions();
  ClearGLError(gl);
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  auto format = caps->getTextureFormat(pixelFormat).sizedFormat;
  switch (caps->msFBOType) {
    case MSFBOType::Standard:
      gl->renderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, format, width, height);
      break;
    case MSFBOType::ES_Apple:
      gl->renderbufferStorageMultisampleAPPLE(GL_RENDERBUFFER, sampleCount, format, width, height);
      break;
    case MSFBOType::ES_EXT_MsToTexture:
    case MSFBOType::ES_IMG_MsToTexture:
      gl->renderbufferStorageMultisampleEXT(GL_RENDERBUFFER, sampleCount, format, width, height);
      break;
    case MSFBOType::None:
      LOGE("Shouldn't be here if we don't support multisampled renderbuffers.");
      break;
  }
  return CheckGLError(gl);
}

static void FrameBufferTexture2D(GLGPU* gpu, unsigned textureTarget, unsigned textureID,
                                 int sampleCount) {
  auto gl = gpu->functions();
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  // Unbinding framebufferTexture2DMultisample on Huawei devices can cause a crash, so always use
  // framebufferTexture2D for unbinding.
  if (textureID != 0 && sampleCount > 1 && caps->usesImplicitMSAAResolve()) {
    gl->framebufferTexture2DMultisample(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureTarget,
                                        textureID, 0, sampleCount);
  } else {
    gl->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureTarget, textureID, 0);
  }
}

static void ReleaseResource(GLGPU* gpu, unsigned frameBufferRead, unsigned frameBufferDraw,
                            unsigned renderBufferID) {
  auto gl = gpu->functions();
  if (frameBufferRead > 0) {
    gl->deleteFramebuffers(1, &frameBufferRead);
    if (frameBufferDraw && frameBufferDraw == frameBufferRead) {
      frameBufferDraw = 0;
    }
    frameBufferRead = 0;
  }
  if (frameBufferDraw > 0) {
    gl->bindFramebuffer(GL_FRAMEBUFFER, frameBufferDraw);
    gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);
    gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->deleteFramebuffers(1, &frameBufferDraw);
    frameBufferDraw = 0;
  }
  if (renderBufferID > 0) {
    gl->deleteRenderbuffers(1, &renderBufferID);
    renderBufferID = 0;
  }
}

static bool CreateRenderBuffer(GLGPU* gpu, GPUTexture* texture, int width, int height,
                               int sampleCount, unsigned* frameBufferID, unsigned* renderBufferID) {
  auto gl = gpu->functions();
  gl->genFramebuffers(1, frameBufferID);
  if (*frameBufferID == 0) {
    return false;
  }
  gl->genRenderbuffers(1, renderBufferID);
  if (*renderBufferID == 0) {
    return false;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, *renderBufferID);
  if (!RenderbufferStorageMSAA(gpu, sampleCount, texture->format(), width, height)) {
    return false;
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, *frameBufferID);
  gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                              *renderBufferID);
#ifdef TGFX_BUILD_FOR_WEB
  return true;
#else
  return gl->checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
#endif
}

std::unique_ptr<GLTextureFrameBuffer> GLTextureFrameBuffer::MakeFrom(GLGPU* gpu,
                                                                     GPUTexture* texture, int width,
                                                                     int height, int sampleCount) {
  if (gpu == nullptr || texture == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  if (!caps->isFormatRenderable(texture->format())) {
    return nullptr;
  }
  auto gl = gpu->functions();
  unsigned frameBufferRead = 0;
  gl->genFramebuffers(1, &frameBufferRead);
  if (frameBufferRead == 0) {
    return nullptr;
  }
  unsigned frameBufferDraw = 0;
  unsigned renderBufferId = 0;
  if (sampleCount > 1 && caps->usesMSAARenderBuffers()) {
    if (!CreateRenderBuffer(gpu, texture, width, height, sampleCount, &frameBufferDraw,
                            &renderBufferId)) {
      ReleaseResource(gpu, frameBufferRead, frameBufferDraw, renderBufferId);
      return nullptr;
    }
  } else {
    frameBufferDraw = frameBufferRead;
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, frameBufferRead);
  auto glTexture = static_cast<GLTexture*>(texture);
  FrameBufferTexture2D(gpu, glTexture->target(), glTexture->id(), sampleCount);
#ifndef TGFX_BUILD_FOR_WEB
  if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    ReleaseResource(gpu, frameBufferRead, frameBufferDraw, renderBufferId);
    return nullptr;
  }
#endif
  return std::unique_ptr<GLTextureFrameBuffer>(
      new GLTextureFrameBuffer(frameBufferRead, frameBufferDraw, renderBufferId, texture->format(),
                               sampleCount, glTexture->target()));
}

void GLTextureFrameBuffer::release(GPU* gpu) {
  auto glGPU = static_cast<GLGPU*>(gpu);
  auto gl = glGPU->functions();
  gl->bindFramebuffer(GL_FRAMEBUFFER, _readFrameBufferID);
  FrameBufferTexture2D(glGPU, textureTarget, 0, _sampleCount);
  gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
  ReleaseResource(glGPU, _readFrameBufferID, _drawFrameBufferID, renderBufferID);
}
}  // namespace tgfx
