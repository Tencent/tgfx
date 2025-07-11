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

#include "GLTextureRenderTarget.h"
#include "core/utils/PixelFormatUtil.h"
#include "core/utils/UniqueID.h"
#include "gpu/Gpu.h"
#include "gpu/TextureSampler.h"
#include "gpu/opengl/GLTextureSampler.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
static ScratchKey ComputeRenderTargetScratchKey(int width, int height, PixelFormat format,
                                                int sampleCount, bool mipmapped) {
  static const uint32_t TextureRenderTargetType = UniqueID::Next();
  BytesKey bytesKey(5);
  bytesKey.write(TextureRenderTargetType);
  bytesKey.write(width);
  bytesKey.write(height);
  bytesKey.write(sampleCount);
  auto formatValue = static_cast<uint32_t>(format);
  auto mipmapValue = static_cast<uint32_t>(mipmapped ? 1 : 0);
  bytesKey.write(formatValue | (mipmapValue << 30));
  return bytesKey;
}

std::shared_ptr<RenderTarget> RenderTarget::MakeFrom(Context* context,
                                                     const BackendTexture& backendTexture,
                                                     int sampleCount, ImageOrigin origin,
                                                     bool adopted) {
  auto sampler = TextureSampler::MakeFrom(context, backendTexture, adopted);
  if (sampler == nullptr) {
    return nullptr;
  }
  sampleCount = context->caps()->getSampleCount(sampleCount, sampler->format());
  ScratchKey scratchKey = {};
  if (adopted) {
    scratchKey =
        ComputeRenderTargetScratchKey(backendTexture.width(), backendTexture.height(),
                                      sampler->format(), sampleCount, sampler->hasMipmaps());
  }
  return GLTextureRenderTarget::MakeFrom(context, std::move(sampler), backendTexture.width(),
                                         backendTexture.height(), sampleCount, origin, scratchKey);
}

std::shared_ptr<RenderTarget> RenderTarget::MakeFrom(Context* context,
                                                     HardwareBufferRef hardwareBuffer,
                                                     int sampleCount) {
  auto format = TextureSampler::GetPixelFormat(hardwareBuffer);
  if (format == PixelFormat::Unknown) {
    return nullptr;
  }
  auto size = HardwareBufferGetSize(hardwareBuffer);
  if (size.isEmpty()) {
    return nullptr;
  }
  auto samplers = TextureSampler::MakeFrom(context, hardwareBuffer);
  if (samplers.size() != 1) {
    return nullptr;
  }
  sampleCount = context->caps()->getSampleCount(sampleCount, format);
  return GLTextureRenderTarget::MakeFrom(context, std::move(samplers.front()), size.width,
                                         size.height, sampleCount);
}

std::shared_ptr<RenderTarget> RenderTarget::Make(Context* context, int width, int height,
                                                 PixelFormat format, int sampleCount,
                                                 bool mipmapped, ImageOrigin origin) {
  if (!Texture::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  auto hasMipmaps = caps->mipmapSupport ? mipmapped : false;
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto scratchKey = ComputeRenderTargetScratchKey(width, height, format, sampleCount, hasMipmaps);
  if (auto renderTarget = Resource::Find<GLTextureRenderTarget>(context, scratchKey)) {
    renderTarget->_origin = origin;
    return renderTarget;
  }
  auto sampler = TextureSampler::Make(context, width, height, format, hasMipmaps);
  if (sampler == nullptr) {
    return nullptr;
  }
  return GLTextureRenderTarget::MakeFrom(context, std::move(sampler), width, height, sampleCount,
                                         origin, scratchKey);
}

static bool RenderbufferStorageMSAA(Context* context, int sampleCount, PixelFormat pixelFormat,
                                    int width, int height) {
  ClearGLError(context);
  auto gl = GLFunctions::Get(context);
  auto caps = GLCaps::Get(context);
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
  return CheckGLError(context);
}

static void FrameBufferTexture2D(Context* context, unsigned textureTarget, unsigned textureID,
                                 int sampleCount) {
  auto gl = GLFunctions::Get(context);
  auto caps = GLCaps::Get(context);
  // Unbinding framebufferTexture2DMultisample on Huawei devices can cause a crash, so always use
  // framebufferTexture2D for unbinding.
  if (textureID != 0 && sampleCount > 1 && caps->usesImplicitMSAAResolve()) {
    gl->framebufferTexture2DMultisample(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureTarget,
                                        textureID, 0, sampleCount);
  } else {
    gl->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureTarget, textureID, 0);
  }
}

static void ReleaseResource(Context* context, unsigned frameBufferRead, unsigned frameBufferDraw,
                            unsigned renderBufferID) {
  auto gl = GLFunctions::Get(context);
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

static bool CreateRenderBuffer(Context* context, TextureSampler* sampler, int width, int height,
                               int sampleCount, unsigned* frameBufferID, unsigned* renderBufferID) {
  auto gl = GLFunctions::Get(context);
  gl->genFramebuffers(1, frameBufferID);
  if (*frameBufferID == 0) {
    return false;
  }
  gl->genRenderbuffers(1, renderBufferID);
  if (*renderBufferID == 0) {
    return false;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, *renderBufferID);
  if (!RenderbufferStorageMSAA(context, sampleCount, sampler->format(), width, height)) {
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

std::shared_ptr<RenderTarget> GLTextureRenderTarget::MakeFrom(
    Context* context, std::unique_ptr<TextureSampler> sampler, int width, int height,
    int sampleCount, ImageOrigin origin, const ScratchKey& scratchKey) {
  DEBUG_ASSERT(context != nullptr);
  DEBUG_ASSERT(sampler != nullptr);
  auto caps = GLCaps::Get(context);
  auto glSampler = static_cast<const GLTextureSampler*>(sampler.get());
  if (!caps->isFormatRenderable(glSampler->format())) {
    sampler->releaseGPU(context);
    return nullptr;
  }
  auto gl = GLFunctions::Get(context);
  unsigned frameBufferRead = 0;
  gl->genFramebuffers(1, &frameBufferRead);
  if (frameBufferRead == 0) {
    sampler->releaseGPU(context);
    return nullptr;
  }
  unsigned frameBufferDraw = 0;
  unsigned renderBufferId = 0;
  if (sampleCount > 1 && caps->usesMSAARenderBuffers()) {
    if (!CreateRenderBuffer(context, sampler.get(), width, height, sampleCount, &frameBufferDraw,
                            &renderBufferId)) {
      ReleaseResource(context, frameBufferRead, frameBufferDraw, renderBufferId);
      sampler->releaseGPU(context);
      return nullptr;
    }
  } else {
    frameBufferDraw = frameBufferRead;
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, frameBufferRead);
  FrameBufferTexture2D(context, glSampler->target(), glSampler->id(), sampleCount);
#ifndef TGFX_BUILD_FOR_WEB
  if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    ReleaseResource(context, frameBufferRead, frameBufferDraw, renderBufferId);
    sampler->releaseGPU(context);
    return nullptr;
  }
#endif
  auto renderTarget =
      new GLTextureRenderTarget(std::move(sampler), width, height, origin, sampleCount);
  renderTarget->_readFrameBufferID = frameBufferRead;
  renderTarget->_drawFrameBufferID = frameBufferDraw;
  renderTarget->renderBufferID = renderBufferId;
  return Resource::AddToCache(context, renderTarget, scratchKey);
}

void GLTextureRenderTarget::onReleaseGPU() {
  auto glSampler = static_cast<const GLTextureSampler*>(_sampler.get());
  auto gl = GLFunctions::Get(context);
  gl->bindFramebuffer(GL_FRAMEBUFFER, _readFrameBufferID);
  FrameBufferTexture2D(context, glSampler->target(), 0, _sampleCount);
  gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
  ReleaseResource(context, _readFrameBufferID, _drawFrameBufferID, renderBufferID);
  _sampler->releaseGPU(context);
}
}  // namespace tgfx
