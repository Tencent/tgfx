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
#include "platform/apple/NV12HardwareBuffer.h"

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

std::vector<PixelFormat> EAGLGPU::getHardwareTextureFormats(HardwareBufferRef hardwareBuffer,
                                                            YUVFormat* yuvFormat) const {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  auto pixelFormat = CVPixelBufferGetPixelFormatType(hardwareBuffer);
  std::vector<PixelFormat> formats = {};
  switch (pixelFormat) {
    case kCVPixelFormatType_OneComponent8:
      formats.push_back(PixelFormat::ALPHA_8);
      break;
    case kCVPixelFormatType_32BGRA:
      formats.push_back(PixelFormat::BGRA_8888);
      break;
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
      formats.push_back(PixelFormat::GRAY_8);
      formats.push_back(PixelFormat::RG_88);
      break;
    default:
      break;
  }
  if (yuvFormat != nullptr) {
    if (formats.size() == 2) {
      *yuvFormat = YUVFormat::NV12;
    } else {
      *yuvFormat = YUVFormat::Unknown;
    }
  }
  return formats;
}

std::vector<std::shared_ptr<GPUTexture>> EAGLGPU::importHardwareTextures(
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
