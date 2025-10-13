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
std::vector<std::shared_ptr<GPUTexture>> CGLHardwareTexture::MakeFrom(
    GLGPU* gpu, CVPixelBufferRef pixelBuffer, uint32_t usage,
    CVOpenGLTextureCacheRef textureCache) {
  if (textureCache == nil) {
    return {};
  }
  auto format = CVPixelBufferGetPixelFormatType(pixelBuffer) == kCVPixelFormatType_OneComponent8
                    ? PixelFormat::ALPHA_8
                    : PixelFormat::RGBA_8888;
  if (usage & GPUTextureUsage::RENDER_ATTACHMENT && !gpu->caps()->isFormatRenderable(format)) {
    return {};
  }
  CVOpenGLTextureRef texture = nil;
  CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache, pixelBuffer,
                                             nullptr, &texture);
  if (texture == nil) {
    return {};
  }
  auto target = CVOpenGLTextureGetTarget(texture);
  auto textureID = CVOpenGLTextureGetName(texture);
  GPUTextureDescriptor descriptor = {static_cast<int>(CVPixelBufferGetWidth(pixelBuffer)),
                                     static_cast<int>(CVPixelBufferGetHeight(pixelBuffer)),
                                     format,
                                     false,
                                     1,
                                     usage};
  auto glTexture = gpu->makeResource<CGLHardwareTexture>(descriptor, pixelBuffer, textureCache,
                                                         target, textureID);
  glTexture->texture = texture;
  if (usage & GPUTextureUsage::RENDER_ATTACHMENT && !glTexture->checkFrameBuffer(gpu)) {
    return {};
  }
  std::vector<std::shared_ptr<GPUTexture>> textures = {};
  textures.push_back(std::move(glTexture));
  return textures;
}

CGLHardwareTexture::CGLHardwareTexture(const GPUTextureDescriptor& descriptor,
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
  }
}

void CGLHardwareTexture::onReleaseTexture(GLGPU*) {
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
