/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "opengl/GLSampler.h"
#include "utils/UniqueID.h"

namespace tgfx {
std::shared_ptr<CGLHardwareTexture> CGLHardwareTexture::MakeFrom(
    Context* context, CVPixelBufferRef pixelBuffer, CVOpenGLTextureCacheRef textureCache) {
  auto scratchKey = ComputeScratchKey(pixelBuffer);
  auto glTexture = Resource::Find<CGLHardwareTexture>(context, scratchKey);
  if (glTexture) {
    return glTexture;
  }
  if (textureCache == nil) {
    return nullptr;
  }
  CVOpenGLTextureRef texture = nil;
  CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache, pixelBuffer,
                                             nullptr, &texture);
  if (texture == nil) {
    return nullptr;
  }
  auto glSampler = std::make_unique<GLSampler>();
  glSampler->target = CVOpenGLTextureGetTarget(texture);
  glSampler->id = CVOpenGLTextureGetName(texture);
  glSampler->format =
      CVPixelBufferGetPixelFormatType(pixelBuffer) == kCVPixelFormatType_OneComponent8
          ? PixelFormat::ALPHA_8
          : PixelFormat::RGBA_8888;
  glTexture = Resource::AddToCache(context, new CGLHardwareTexture(pixelBuffer), scratchKey);
  glTexture->sampler = std::move(glSampler);
  glTexture->texture = texture;
  glTexture->textureCache = textureCache;
  CFRetain(textureCache);

  return glTexture;
}

ScratchKey CGLHardwareTexture::ComputeScratchKey(CVPixelBufferRef pixelBuffer) {
  static const uint32_t CGLHardwareTextureType = UniqueID::Next();
  ScratchKey scratchKey = {};
  scratchKey.write(CGLHardwareTextureType);
  // The pointer can be used as the key directly because the cache holder retains the CVPixelBuffer.
  // As long as the holder cache exists, the CVPixelBuffer pointer remains valid, avoiding conflicts
  // with new objects. In other scenarios, it's best to avoid using pointers as keys.
  scratchKey.write(pixelBuffer);
  return scratchKey;
}

CGLHardwareTexture::CGLHardwareTexture(CVPixelBufferRef pixelBuffer)
    : Texture(static_cast<int>(CVPixelBufferGetWidth(pixelBuffer)),
              static_cast<int>(CVPixelBufferGetHeight(pixelBuffer)), ImageOrigin::TopLeft),
      pixelBuffer(pixelBuffer) {
  CFRetain(pixelBuffer);
}

CGLHardwareTexture::~CGLHardwareTexture() {
  CFRelease(pixelBuffer);
  if (texture != nil) {
    CFRelease(texture);
    CFRelease(textureCache);
  }
}

size_t CGLHardwareTexture::memoryUsage() const {
  return CVPixelBufferGetDataSize(pixelBuffer);
}

void CGLHardwareTexture::onReleaseGPU() {
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
