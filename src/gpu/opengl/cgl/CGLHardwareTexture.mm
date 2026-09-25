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
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
namespace {
// Native import calls must not invalidate the bindings cached by GLState.
class ScopedCGLImport {
 public:
  explicit ScopedCGLImport(const GLFunctions* gl) : gl(gl) {
    gl->getIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    gl->getIntegerv(GL_TEXTURE_BINDING_2D, &texture2D);
    gl->getIntegerv(GL_TEXTURE_BINDING_RECTANGLE, &textureRect);
    gl->getIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    gl->getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    gl->getIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
  }

  ~ScopedCGLImport() {
    if (copyTexture != 0) {
      gl->deleteTextures(1, &copyTexture);
    }
    if (copyFramebuffer != 0) {
      gl->deleteFramebuffers(1, &copyFramebuffer);
    }
    if (texture != nullptr) {
      CVOpenGLTextureRelease(texture);
    }
    gl->activeTexture(static_cast<unsigned>(activeTexture));
    gl->bindTexture(GL_TEXTURE_2D, static_cast<unsigned>(texture2D));
    gl->bindTexture(GL_TEXTURE_RECTANGLE, static_cast<unsigned>(textureRect));
    gl->bindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned>(readFramebuffer));
    gl->bindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned>(drawFramebuffer));
    gl->bindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<unsigned>(unpackBuffer));
  }

  ScopedCGLImport(const ScopedCGLImport&) = delete;
  ScopedCGLImport& operator=(const ScopedCGLImport&) = delete;

  CVOpenGLTextureRef texture = nullptr;
  unsigned copyTexture = 0;
  unsigned copyFramebuffer = 0;

 private:
  const GLFunctions* gl;
  int activeTexture = 0;
  int texture2D = 0;
  int textureRect = 0;
  int readFramebuffer = 0;
  int drawFramebuffer = 0;
  int unpackBuffer = 0;
};
}  // namespace

std::vector<std::shared_ptr<Texture>> CGLHardwareTexture::MakeFrom(
    GLGPU* gpu, CVPixelBufferRef pixelBuffer, uint32_t usage,
    CVOpenGLTextureCacheRef textureCache) {
  if (gpu == nullptr || pixelBuffer == nullptr || textureCache == nullptr || usage == 0) {
    return {};
  }
  auto pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
  if (pixelFormat != kCVPixelFormatType_OneComponent8 && pixelFormat != kCVPixelFormatType_32BGRA) {
    return {};
  }
  auto format = pixelFormat == kCVPixelFormatType_OneComponent8 ? PixelFormat::ALPHA_8
                                                                : PixelFormat::BGRA_8888;
  // The copy source needs a complete read framebuffer even for sampling-only imports.
  if (!gpu->isFormatRenderable(format)) {
    return {};
  }
  auto width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
  auto height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));
  TextureDescriptor descriptor = {width, height, format, false, 1, usage};
  auto gl = gpu->functions();
  std::shared_ptr<CGLHardwareTexture> glTexture = nullptr;
  {
    ScopedCGLImport resources(gl);
    ClearGLError(gl);
    auto result = CVOpenGLTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, textureCache, pixelBuffer, nullptr, &resources.texture);
    if (result != kCVReturnSuccess || resources.texture == nullptr) {
      return {};
    }
    auto target = static_cast<unsigned>(CVOpenGLTextureGetTarget(resources.texture));
    auto textureID = static_cast<unsigned>(CVOpenGLTextureGetName(resources.texture));
    if (textureID == 0 || !CheckGLError(gl)) {
      return {};
    }
    // CVOpenGLTextureCache only ever yields GL_TEXTURE_RECTANGLE objects, and rectangle
    // textures cannot share the precompiled pipeline with regular two-dimensional ones
    // (per-sampler type declarations, no hardware wrap modes). Copy the surface into a plain
    // GL_TEXTURE_2D once at import: the pixel buffer keeps its zero-copy path on Metal, and
    // every texture the GL context produces stays two-dimensional. The copy runs inside
    // ScopedCGLImport, so every GL binding it touches is restored before returning.
    gl->genFramebuffers(1, &resources.copyFramebuffer);
    if (resources.copyFramebuffer == 0) {
      return {};
    }
    gl->bindFramebuffer(GL_READ_FRAMEBUFFER, resources.copyFramebuffer);
    gl->framebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, textureID, 0);
    auto status = gl->checkFramebufferStatus(GL_READ_FRAMEBUFFER);
    if (!CheckGLError(gl) || status != GL_FRAMEBUFFER_COMPLETE) {
      LOGE("CGLHardwareTexture::MakeFrom() incomplete copy source framebuffer!");
      return {};
    }
    gl->genTextures(1, &resources.copyTexture);
    if (resources.copyTexture == 0) {
      return {};
    }
    gl->bindTexture(GL_TEXTURE_2D, resources.copyTexture);
    gl->bindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    const auto& textureFormat = gpu->caps()->getTextureFormat(format);
    gl->texImage2D(GL_TEXTURE_2D, 0, static_cast<int>(textureFormat.internalFormatTexImage), width,
                   height, 0, textureFormat.externalFormat, textureFormat.externalType, nullptr);
    if (!CheckGLError(gl)) {
      return {};
    }
    gl->copyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    if (!CheckGLError(gl)) {
      return {};
    }
    glTexture = gpu->makeResource<CGLHardwareTexture>(descriptor, pixelBuffer, textureCache,
                                                      static_cast<unsigned>(GL_TEXTURE_2D),
                                                      resources.copyTexture);
    resources.copyTexture = 0;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT && !glTexture->checkFrameBuffer(gpu)) {
    return {};
  }
  return {std::move(glTexture)};
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

void CGLHardwareTexture::onReleaseTexture(GLGPU* gpu) {
  // The 2D copy texture was created by us rather than owned by CoreVideo, so it must go
  // through the base GLTexture release (glDeleteTextures) instead of only flushing the
  // CV texture cache, which never deletes it.
  GLTexture::onReleaseTexture(gpu);
  if (textureCache != nil) {
    CVOpenGLTextureCacheFlush(textureCache, 0);
    CFRelease(textureCache);
    textureCache = nil;
  }
}
}  // namespace tgfx
