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
#include "gpu/GPU.h"
#include "gpu/GPUTexture.h"
#include "gpu/opengl/GLTexture.h"
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
  if (context == nullptr) {
    return nullptr;
  }
  auto texture = context->gpu()->importExternalTexture(backendTexture, adopted);
  if (texture == nullptr) {
    return nullptr;
  }
  sampleCount = context->caps()->getSampleCount(sampleCount, texture->format());
  ScratchKey scratchKey = {};
  if (adopted) {
    scratchKey =
        ComputeRenderTargetScratchKey(backendTexture.width(), backendTexture.height(),
                                      texture->format(), sampleCount, texture->hasMipmaps());
  }
  return GLTextureRenderTarget::MakeFrom(context, std::move(texture), backendTexture.width(),
                                         backendTexture.height(), sampleCount, origin, !adopted,
                                         scratchKey);
}

std::shared_ptr<RenderTarget> RenderTarget::MakeFrom(Context* context,
                                                     HardwareBufferRef hardwareBuffer,
                                                     int sampleCount) {
  if (context == nullptr) {
    return nullptr;
  }
  auto size = HardwareBufferGetSize(hardwareBuffer);
  if (size.isEmpty()) {
    return nullptr;
  }
  auto gpu = context->gpu();
  YUVFormat yuvFormat = YUVFormat::Unknown;
  auto formats = gpu->getHardwareTextureFormats(hardwareBuffer, &yuvFormat);
  if (formats.size() != 1 || yuvFormat != YUVFormat::Unknown) {
    return nullptr;
  }
  auto textures = gpu->importHardwareTextures(hardwareBuffer);
  if (textures.size() != 1) {
    return nullptr;
  }
  sampleCount = context->caps()->getSampleCount(sampleCount, formats.front());
  return GLTextureRenderTarget::MakeFrom(context, std::move(textures.front()), size.width,
                                         size.height, sampleCount, ImageOrigin::TopLeft, true);
}

std::shared_ptr<RenderTarget> RenderTarget::Make(Context* context, int width, int height,
                                                 PixelFormat format, int sampleCount,
                                                 bool mipmapped, ImageOrigin origin) {
  if (!TextureView::CheckSizeAndFormat(context, width, height, format)) {
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
  auto texture = context->gpu()->createTexture(width, height, format, hasMipmaps);
  if (texture == nullptr) {
    return nullptr;
  }
  return GLTextureRenderTarget::MakeFrom(context, std::move(texture), width, height, sampleCount,
                                         origin, false, scratchKey);
}

static bool RenderbufferStorageMSAA(Context* context, int sampleCount, PixelFormat pixelFormat,
                                    int width, int height) {
  auto gl = GLFunctions::Get(context);
  ClearGLError(gl);
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
  return CheckGLError(gl);
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

static bool CreateRenderBuffer(Context* context, GPUTexture* texture, int width, int height,
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
  if (!RenderbufferStorageMSAA(context, sampleCount, texture->format(), width, height)) {
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
    Context* context, std::unique_ptr<GPUTexture> texture, int width, int height, int sampleCount,
    ImageOrigin origin, bool externallyOwned, const ScratchKey& scratchKey) {
  DEBUG_ASSERT(context != nullptr);
  DEBUG_ASSERT(texture != nullptr);
  auto caps = GLCaps::Get(context);
  auto glTexture = static_cast<const GLTexture*>(texture.get());
  if (!caps->isFormatRenderable(glTexture->format())) {
    texture->release(context->gpu());
    return nullptr;
  }
  auto gl = GLFunctions::Get(context);
  unsigned frameBufferRead = 0;
  gl->genFramebuffers(1, &frameBufferRead);
  if (frameBufferRead == 0) {
    texture->release(context->gpu());
    return nullptr;
  }
  unsigned frameBufferDraw = 0;
  unsigned renderBufferId = 0;
  if (sampleCount > 1 && caps->usesMSAARenderBuffers()) {
    if (!CreateRenderBuffer(context, texture.get(), width, height, sampleCount, &frameBufferDraw,
                            &renderBufferId)) {
      ReleaseResource(context, frameBufferRead, frameBufferDraw, renderBufferId);
      texture->release(context->gpu());
      return nullptr;
    }
  } else {
    frameBufferDraw = frameBufferRead;
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, frameBufferRead);
  FrameBufferTexture2D(context, glTexture->target(), glTexture->id(), sampleCount);
#ifndef TGFX_BUILD_FOR_WEB
  if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    ReleaseResource(context, frameBufferRead, frameBufferDraw, renderBufferId);
    texture->release(context->gpu());
    return nullptr;
  }
#endif
  auto renderTarget = new GLTextureRenderTarget(std::move(texture), width, height, origin,
                                                sampleCount, externallyOwned);
  renderTarget->_readFrameBufferID = frameBufferRead;
  renderTarget->_drawFrameBufferID = frameBufferDraw;
  renderTarget->renderBufferID = renderBufferId;
  return Resource::AddToCache(context, renderTarget, scratchKey);
}

void GLTextureRenderTarget::onReleaseGPU() {
  auto glTexture = static_cast<const GLTexture*>(_texture.get());
  auto gl = GLFunctions::Get(context);
  gl->bindFramebuffer(GL_FRAMEBUFFER, _readFrameBufferID);
  FrameBufferTexture2D(context, glTexture->target(), 0, _sampleCount);
  gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
  ReleaseResource(context, _readFrameBufferID, _drawFrameBufferID, renderBufferID);
  _texture->release(context->gpu());
}
}  // namespace tgfx
