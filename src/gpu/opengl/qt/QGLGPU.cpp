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

#ifdef __APPLE__
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include "gpu/opengl/cgl/CGLHardwareTexture.h"
#endif
#include "gpu/opengl/qt/QGLGPU.h"

namespace tgfx {
#ifdef __APPLE__

bool HardwareBufferAvailable() {
  return true;
}

std::vector<std::shared_ptr<Texture>> QGLGPU::importHardwareTextures(
    HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  return CGLHardwareTexture::MakeFrom(this, hardwareBuffer, usage, getTextureCache());
}

QGLGPU::~QGLGPU() {
  if (textureCache != nil) {
    CFRelease(textureCache);
    textureCache = nil;
  }
}

CVOpenGLTextureCacheRef QGLGPU::getTextureCache() {
  if (!textureCache) {
    // The toolchain of QT does not allow us to access the native OpenGL interface directly.
    typedef CGLContextObj (*GetCurrentContext)();
    typedef CGLPixelFormatObj (*GetPixelFormat)(CGLContextObj);
    auto getCurrentContext =
        reinterpret_cast<GetCurrentContext>(dlsym(RTLD_DEFAULT, "CGLGetCurrentContext"));
    auto getPixelFormat =
        reinterpret_cast<GetPixelFormat>(dlsym(RTLD_DEFAULT, "CGLGetPixelFormat"));
    auto cglContext = getCurrentContext();
    auto pixelFormatObj = getPixelFormat(cglContext);
    CVOpenGLTextureCacheCreate(kCFAllocatorDefault, NULL, cglContext, pixelFormatObj, NULL,
                               &textureCache);
  }
  return textureCache;
}

#else

bool HardwareBufferAvailable() {
  return false;
}

std::vector<std::shared_ptr<Texture>> QGLGPU::importHardwareTextures(HardwareBufferRef, uint32_t) {
  return {};
}

#endif
}  // namespace tgfx