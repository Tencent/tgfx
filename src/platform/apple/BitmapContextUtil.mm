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

#include "BitmapContextUtil.h"
#include "tgfx/core/ColorSpace.h"

namespace tgfx {
static uint32_t GetBitmapInfo(AlphaType alphaType, ColorType colorType) {
  CGBitmapInfo bitmapInfo = 0;
  auto premultiplied = alphaType == AlphaType::Premultiplied;
  CGImageAlphaInfo alphaInfo = kCGImageAlphaNone;
  switch (colorType) {
    case ColorType::BGRA_8888:
      bitmapInfo = kCGBitmapByteOrder32Little;
      alphaInfo = premultiplied ? kCGImageAlphaPremultipliedFirst : kCGImageAlphaFirst;
      break;
    case ColorType::RGBA_8888:
      bitmapInfo = kCGBitmapByteOrder32Big;
      alphaInfo = premultiplied ? kCGImageAlphaPremultipliedLast : kCGImageAlphaLast;
      break;
    case ColorType::ALPHA_8:
      alphaInfo = kCGImageAlphaOnly;
      break;
    default:
      break;
  }
  return static_cast<uint32_t>(bitmapInfo) | static_cast<uint32_t>(alphaInfo);
}

static CGColorSpaceRef CreateCGColorSpace(const std::shared_ptr<ColorSpace>& colorSpace) {
  if (colorSpace != nullptr) {
    if (auto iccData = colorSpace->toICCProfile()) {
      if (CFDataRef cfData =
              CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(iccData->data()),
                           static_cast<CFIndex>(iccData->size()))) {
        CGColorSpaceRef cgColorSpace = nullptr;
        if (@available(iOS 10.0, macOS 10.12, *)) {
          cgColorSpace = CGColorSpaceCreateWithICCData(cfData);
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
          cgColorSpace = CGColorSpaceCreateWithICCProfile(cfData);
#pragma clang diagnostic pop
        }
        CFRelease(cfData);
        if (cgColorSpace != nullptr) {
          return cgColorSpace;
        }
      }
    }
  }
  return CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
}

CGContextRef CreateBitmapContext(const ImageInfo& info, void* pixels) {
  if (pixels == nullptr) {
    return nullptr;
  }
  auto bitmapInfo = GetBitmapInfo(info.alphaType(), info.colorType());
  if (bitmapInfo == 0) {
    return nullptr;
  }
  CGColorSpaceRef colorspace =
      info.colorType() == ColorType::ALPHA_8 ? nullptr : CreateCGColorSpace(info.colorSpace());
  auto cgContext = CGBitmapContextCreate(pixels, static_cast<size_t>(info.width()),
                                         static_cast<size_t>(info.height()), 8, info.rowBytes(),
                                         colorspace, bitmapInfo);
  if (colorspace) {
    CFRelease(colorspace);
  }
  return cgContext;
}
}  // namespace tgfx