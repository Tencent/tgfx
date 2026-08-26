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

#include "CGLHardwareTexture.h"
#include "core/utils/UniqueID.h"
#include "gpu/opengl/GLGPU.h"

namespace tgfx {
std::vector<std::shared_ptr<Texture>> CGLHardwareTexture::MakeFrom(
    GLGPU* gpu, CVPixelBufferRef pixelBuffer, uint32_t usage,
    CVOpenGLTextureCacheRef textureCache) {
  if (textureCache == nil) {
    return {};
  }
  auto format = CVPixelBufferGetPixelFormatType(pixelBuffer) == kCVPixelFormatType_OneComponent8
                    ? PixelFormat::ALPHA_8
                    : PixelFormat::RGBA_8888;
  if (usage & TextureUsage::RENDER_ATTACHMENT && !gpu->isFormatRenderable(format)) {
    return {};
  }
  CVOpenGLTextureRef texture = nullptr;
  CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache, pixelBuffer,
                                             nullptr, &texture);
  if (texture == nullptr) {
    return {};
  }
  // CVOpenGLTextureCache only ever yields GL_TEXTURE_RECTANGLE objects, and rectangle textures
  // cannot share the precompiled pipeline with regular two-dimensional ones (per-sampler type
  // declarations, no hardware wrap modes). Since macOS desktop OpenGL is a debugging backend
  // here, copy the surface into a plain GL_TEXTURE_2D once at import: the pixel buffer keeps its
  // zero-copy path on Metal, and every texture the GL context produces stays two-dimensional.
  auto gl = gpu->functions();
  auto width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
  auto height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));
  auto rectTarget = CVOpenGLTextureGetTarget(texture);
  auto rectID = CVOpenGLTextureGetName(texture);
  GLint boundFBO = 0;
  gl->getIntegerv(GL_FRAMEBUFFER_BINDING, &boundFBO);
  unsigned copyFBO = 0;
  gl->genFramebuffers(1, &copyFBO);
  gl->bindFramebuffer(GL_FRAMEBUFFER, copyFBO);
  gl->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rectTarget, rectID, 0);
  unsigned twoDID = 0;
  gl->genTextures(1, &twoDID);
  gl->bindTexture(GL_TEXTURE_2D, twoDID);
  const auto& textureFormat = gpu->caps()->getTextureFormat(format);
  gl->texImage2D(GL_TEXTURE_2D, 0, static_cast<int>(textureFormat.internalFormatTexImage), width,
                 height, 0, textureFormat.externalFormat, textureFormat.externalType, nullptr);
  gl->copyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
  gl->bindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(boundFBO));
  gl->deleteFramebuffers(1, &copyFBO);
  CVOpenGLTextureRelease(texture);
  if (twoDID == 0) {
    LOGE("CGLHardwareTexture::MakeFrom() failed to copy the pixel buffer into a 2D texture!");
    return {};
  }
  TextureDescriptor descriptor = {width, height, format, false, 1, usage};
  auto glTexture = gpu->makeResource<CGLHardwareTexture>(
      descriptor, pixelBuffer, textureCache, static_cast<unsigned>(GL_TEXTURE_2D), twoDID);
  if (usage & TextureUsage::RENDER_ATTACHMENT && !glTexture->checkFrameBuffer(gpu)) {
    return {};
  }
  std::vector<std::shared_ptr<Texture>> textures = {};
  textures.push_back(std::move(glTexture));
  return textures;
}

CGLHardwareTexture::CGLHardwareTexture(const TextureDescriptor& descriptor,
                                       CVPixelBufferRef pixelBuffer,
                                       CVOpenGLTextureCacheRef textureCache, unsigned target,
                                       unsigned textureID)
    : GLTexture(descriptor, target, textureID),
      pixelBuffer(pixelBuffer),
      textureCache(textureCache) {
  CFRetain(pixelBuffer);
  CFRetain(textureCache);
}

CGLHardwareTexture::~CGLHardwareTexture() {
  CFRelease(pixelBuffer);
  if (textureCache != nil) {
    CFRelease(textureCache);
  }
}

void CGLHardwareTexture::onReleaseTexture(GLGPU*) {
  if (textureCache == nil) {
    return;
  }
  CVOpenGLTextureCacheFlush(textureCache, 0);
  CFRelease(textureCache);
  textureCache = nil;
}
}  // namespace tgfx
