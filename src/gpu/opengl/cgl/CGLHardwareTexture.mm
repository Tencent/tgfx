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

namespace tgfx {
std::unique_ptr<CGLHardwareTexture> CGLHardwareTexture::MakeFrom(
    const Caps* caps, CVPixelBufferRef pixelBuffer, uint32_t usage,
    CVOpenGLTextureCacheRef textureCache) {
  if (textureCache == nil) {
    return nullptr;
  }
  auto format = CVPixelBufferGetPixelFormatType(pixelBuffer) == kCVPixelFormatType_OneComponent8
                    ? PixelFormat::ALPHA_8
                    : PixelFormat::RGBA_8888;
  if (usage & GPUTextureUsage::RENDER_ATTACHMENT && !caps->isFormatRenderable(format)) {
    return nullptr;
  }
  CVOpenGLTextureRef texture = nil;
  CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache, pixelBuffer,
                                             nullptr, &texture);
  if (texture == nil) {
    return nullptr;
  }
  auto target = CVOpenGLTextureGetTarget(texture);
  auto textureID = CVOpenGLTextureGetName(texture);
  GPUTextureDescriptor descriptor = {};
  descriptor.width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
  descriptor.height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));
  descriptor.format = format;
  descriptor.usage = usage;
  auto gpuTexture = std::unique_ptr<CGLHardwareTexture>(
      new CGLHardwareTexture(descriptor, pixelBuffer, textureCache, target, textureID));
  gpuTexture->texture = texture;
  return gpuTexture;
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

void CGLHardwareTexture::onRelease(GLGPU*) {
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
