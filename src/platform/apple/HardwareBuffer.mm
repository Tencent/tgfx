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

#include "tgfx/platform/HardwareBuffer.h"
#import <CoreVideo/CoreVideo.h>
#import <TargetConditionals.h>

namespace tgfx {

bool HardwareBufferCheck(HardwareBufferRef buffer) {
  if (!HardwareBufferAvailable()) {
    return false;
  }
  auto success = CVPixelBufferGetIOSurface(buffer) != nil;
#if TARGET_OS_IPHONE == 0 && TARGET_OS_MAC == 1 && defined(__aarch64__)
  if (success && CVPixelBufferGetPixelFormatType(buffer) == kCVPixelFormatType_OneComponent8) {
    // The alpha-only CVPixelBuffer on macOS with Apple Silicon does not share memory across GPU and
    // CPU.
    return false;
  }
#endif
  return success;
}

HardwareBufferRef HardwareBufferAllocate(int width, int height, bool alphaOnly) {
  if (!HardwareBufferAvailable() || width <= 0 || height <= 0) {
    return nil;
  }
#if TARGET_OS_IPHONE == 0 && TARGET_OS_MAC == 1 && defined(__aarch64__)
  if (alphaOnly) {
    return nil;
  }
#endif
  OSType pixelFormat = alphaOnly ? kCVPixelFormatType_OneComponent8 : kCVPixelFormatType_32BGRA;
  NSDictionary* options = @{(id)kCVPixelBufferIOSurfacePropertiesKey : @{}};
  CVPixelBufferRef pixelBuffer = nil;
  CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, static_cast<size_t>(width),
                                        static_cast<size_t>(height), pixelFormat,
                                        (CFDictionaryRef)options, &pixelBuffer);
  if (status != kCVReturnSuccess) {
    return nil;
  }
  return pixelBuffer;
}

HardwareBufferRef HardwareBufferRetain(HardwareBufferRef buffer) {
  return CVPixelBufferRetain(buffer);
}

void HardwareBufferRelease(HardwareBufferRef buffer) {
  CVPixelBufferRelease(buffer);
}

void* HardwareBufferLock(HardwareBufferRef buffer) {
  CVPixelBufferLockBaseAddress(buffer, 0);
  return CVPixelBufferGetBaseAddress(buffer);
}

void HardwareBufferUnlock(HardwareBufferRef buffer) {
  CVPixelBufferUnlockBaseAddress(buffer, 0);
}

HardwareBufferInfo HardwareBufferGetInfo(HardwareBufferRef buffer) {
  if (buffer == nil) {
    return {};
  }
  auto pixelFormat = CVPixelBufferGetPixelFormatType(buffer);
  HardwareBufferInfo info = {};
  switch (pixelFormat) {
    case kCVPixelFormatType_OneComponent8:
      info.format = HardwareBufferFormat::ALPHA_8;
      break;
    case kCVPixelFormatType_32BGRA:
      info.format = HardwareBufferFormat::BGRA_8888;
      break;
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
      info.format = HardwareBufferFormat::YCBCR_420_SP;
      break;
    default:
      return {};
  }
  info.width = static_cast<int>(CVPixelBufferGetWidth(buffer));
  info.height = static_cast<int>(CVPixelBufferGetHeight(buffer));
  info.rowBytes = CVPixelBufferGetBytesPerRow(buffer);
  return info;
}
}  // namespace tgfx
