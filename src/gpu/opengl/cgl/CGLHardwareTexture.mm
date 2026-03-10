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
#include <OpenGL/gl.h>
#include "core/utils/UniqueID.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLGPU.h"

namespace tgfx {

std::vector<std::shared_ptr<Texture>> CGLHardwareTexture::MakeFrom(
    GLGPU* gpu, CVPixelBufferRef pixelBuffer, uint32_t usage,
    CVOpenGLTextureCacheRef textureCache) {
  if (textureCache == nil) {
    return {};
  }
  auto pixelFormatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
  PixelFormat format = PixelFormat::RGBA_8888;
  if (pixelFormatType == kCVPixelFormatType_OneComponent8) {
    format = PixelFormat::ALPHA_8;
  } else if (pixelFormatType == kCVPixelFormatType_32BGRA) {
    format = PixelFormat::BGRA_8888;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT && !gpu->isFormatRenderable(format)) {
    return {};
  }

  // Flush texture cache before creating texture to ensure we get latest IOSurface data
  CVOpenGLTextureCacheFlush(textureCache, 0);

  // Use CVOpenGLTextureCache to create texture.
  // On Intel Mac returns GL_TEXTURE_RECTANGLE, on Apple Silicon returns GL_TEXTURE_2D.
  CVOpenGLTextureRef cvTexture = nil;
  CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache, pixelBuffer,
                                             nullptr, &cvTexture);
  if (cvTexture == nil) {
    return {};
  }

  auto cvTarget = CVOpenGLTextureGetTarget(cvTexture);
  auto cvTextureID = CVOpenGLTextureGetName(cvTexture);
  int width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
  int height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));

  /**
   * If it's GL_TEXTURE_RECTANGLE, convert to GL_TEXTURE_2D via FBO blit.
   * HOWEVER: If this texture is used for RENDER_ATTACHMENT (render target),
   * we must use the Rectangle texture directly so that rendering writes to IOSurface.
   */
  if (cvTarget == GL_TEXTURE_RECTANGLE_ARB) {
    // For render targets, use Rectangle texture directly to ensure IOSurface is updated
    if (usage & TextureUsage::RENDER_ATTACHMENT) {
      TextureDescriptor descriptor = {width, height, format, false, 1, usage};
      auto glTexture = gpu->makeResource<CGLHardwareTexture>(descriptor, pixelBuffer, textureCache,
                                                             cvTarget, cvTextureID);
      glTexture->texture = cvTexture;
      if (!glTexture->checkFrameBuffer(gpu)) {
        return {};
      }
      std::vector<std::shared_ptr<Texture>> textures = {};
      textures.push_back(std::move(glTexture));
      return textures;
    }

    // For non-render-target usage (read-only), convert to GL_TEXTURE_2D via FBO blit
    auto gl = gpu->functions();
    auto caps = static_cast<const GLCaps*>(gpu->caps());

    // Create GL_TEXTURE_2D
    unsigned texture2D = 0;
    gl->genTextures(1, &texture2D);
    if (texture2D == 0) {
      CFRelease(cvTexture);
      return {};
    }

    gl->bindTexture(GL_TEXTURE_2D, texture2D);
    gl->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Allocate texture storage
    const auto& textureFormat = caps->getTextureFormat(format);
    gl->texImage2D(GL_TEXTURE_2D, 0, static_cast<int>(textureFormat.internalFormatTexImage), width,
                   height, 0, textureFormat.externalFormat, textureFormat.externalType, nullptr);
    gl->bindTexture(GL_TEXTURE_2D, 0);

    // Create FBOs for blit
    unsigned srcFBO = 0, dstFBO = 0;
    gl->genFramebuffers(1, &srcFBO);
    gl->genFramebuffers(1, &dstFBO);

    // Attach Rectangle texture to source FBO
    gl->bindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    gl->framebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB,
                             cvTextureID, 0);

    // Check source FBO completeness
    GLenum srcStatus = gl->checkFramebufferStatus(GL_READ_FRAMEBUFFER);

    // Attach 2D texture to destination FBO
    gl->bindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
    gl->framebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture2D,
                             0);

    // Check destination FBO completeness
    GLenum dstStatus = gl->checkFramebufferStatus(GL_DRAW_FRAMEBUFFER);

    bool blitSuccess = false;
    if (srcStatus == GL_FRAMEBUFFER_COMPLETE && dstStatus == GL_FRAMEBUFFER_COMPLETE) {
      // Blit from Rectangle to 2D
      gl->blitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);

      // Flush to ensure blit is complete
      gl->flush();

      // Check for GL errors
      GLenum blitError = gl->getError();
      if (blitError == GL_NO_ERROR) {
        blitSuccess = true;
      }
    }

    // Cleanup FBOs
    gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->deleteFramebuffers(1, &srcFBO);
    gl->deleteFramebuffers(1, &dstFBO);

    // If blit failed, fall back to using Rectangle texture directly
    if (!blitSuccess) {
      gl->deleteTextures(1, &texture2D);

      TextureDescriptor descriptor = {width, height, format, false, 1, usage};
      auto glTexture = gpu->makeResource<CGLHardwareTexture>(descriptor, pixelBuffer, textureCache,
                                                             cvTarget, cvTextureID);
      glTexture->texture = cvTexture;
      std::vector<std::shared_ptr<Texture>> textures = {};
      textures.push_back(std::move(glTexture));
      return textures;
    }

    // Release the CVOpenGLTexture (we don't need Rectangle texture anymore)
    CFRelease(cvTexture);

    TextureDescriptor descriptor = {width, height, format, false, 1, usage};
    auto glTexture = gpu->makeResource<CGLHardwareTexture>(
        descriptor, pixelBuffer, textureCache, static_cast<unsigned>(GL_TEXTURE_2D), texture2D);
    glTexture->texture = nil;
    glTexture->ownedTextureID = texture2D;

    std::vector<std::shared_ptr<Texture>> textures = {};
    textures.push_back(std::move(glTexture));
    return textures;
  }

  // If it's already GL_TEXTURE_2D, use directly

  TextureDescriptor descriptor = {width, height, format, false, 1, usage};
  auto glTexture = gpu->makeResource<CGLHardwareTexture>(descriptor, pixelBuffer, textureCache,
                                                         cvTarget, cvTextureID);
  glTexture->texture = cvTexture;
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
  if (texture != nil) {
    CFRelease(texture);
    CFRelease(textureCache);
  } else if (textureCache != nil) {
    // Release textureCache even if we're using ownedTextureID mode
    CFRelease(textureCache);
  }
}

void CGLHardwareTexture::onReleaseTexture(GLGPU* gpu) {
  // Handle ownedTextureID (GL_TEXTURE_2D mode)
  if (ownedTextureID != 0) {
    auto gl = gpu->functions();
    gl->deleteTextures(1, &ownedTextureID);
    ownedTextureID = 0;
    return;
  }

  // Handle CVOpenGLTexture mode
  if (texture == nil) {
    return;
  }
  CFRelease(texture);
  texture = nil;
  CVOpenGLTextureCacheFlush(textureCache, 0);
  CFRelease(textureCache);
  textureCache = nil;
}
}  // namespace tgfx
