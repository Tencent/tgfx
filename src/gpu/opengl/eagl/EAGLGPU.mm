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

#include "EAGLGPU.h"
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES3/glext.h>
#import <TargetConditionals.h>
#include "EAGLHardwareTexture.h"
#include "core/PixelBuffer.h"

namespace tgfx {
bool HardwareBufferAvailable() {
#if TARGET_IPHONE_SIMULATOR
  return false;
#else
  return true;
#endif
}

EAGLGPU::~EAGLGPU() {
  if (textureCache != nil) {
    CFRelease(textureCache);
    textureCache = nil;
  }
}

std::vector<std::shared_ptr<Texture>> EAGLGPU::importHardwareTextures(
    HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  return EAGLHardwareTexture::MakeFrom(this, hardwareBuffer, usage);
}

CVOpenGLESTextureCacheRef EAGLGPU::getTextureCache() {
  if (!textureCache) {
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCVOpenGLESTextureCacheMaximumTextureAgeKey,
                         [NSNumber numberWithFloat:0]);
    CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, attrs, _eaglContext, NULL, &textureCache);
    CFRelease(attrs);
  }
  return textureCache;
}
}  // namespace tgfx
