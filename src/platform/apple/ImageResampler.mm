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

#include "platform/ImageResampler.h"
#include <CoreGraphics/CoreGraphics.h>
#include <Foundation/Foundation.h>
#include "BitmapContextUtil.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
static CGInterpolationQuality GetInterpolationQuality(FilterQuality quality) {
  switch (quality) {
    case FilterQuality::None:
      return kCGInterpolationNone;
    case FilterQuality::Low:
      return kCGInterpolationLow;
    case FilterQuality::Medium:
      return kCGInterpolationMedium;
    case FilterQuality::High:
      return kCGInterpolationHigh;
    default:
      return kCGInterpolationDefault;
  }
}

bool ImageResamper::Scale(const ImageInfo& srcInfo, const void* srcPixels, const ImageInfo& dstInfo,
                          void* dstPixels, FilterQuality quality) {
  if (srcInfo.isEmpty() || srcPixels == nullptr || dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  Buffer srcTempBuffer = {};
  auto srcData = srcPixels;
  auto colorType = srcInfo.colorType();
  auto info = srcInfo;
  if (colorType != ColorType::RGBA_8888 && colorType != ColorType::BGRA_8888 &&
      colorType != ColorType::ALPHA_8) {
    info = srcInfo.makeColorType(ColorType::RGBA_8888);
    srcTempBuffer.alloc(info.byteSize());
    Pixmap(srcInfo, srcPixels).readPixels(info, srcTempBuffer.bytes());
    srcData = srcTempBuffer.data();
  }
  auto dataProvider = CGDataProviderCreateWithData(nullptr, srcData, info.byteSize(), nullptr);
  if (!dataProvider) {
    return false;
  }
  auto colorspace =
      info.colorType() == ColorType::ALPHA_8 ? nullptr : CGColorSpaceCreateDeviceRGB();
  auto bitmapInfo = GetBitmapInfo(info.alphaType(), info.colorType());
  CGImageRef image = CGImageCreate(
      static_cast<size_t>(info.width()), static_cast<size_t>(info.height()), 8, 32, info.rowBytes(),
      colorspace, bitmapInfo, dataProvider, nullptr, true, kCGRenderingIntentDefault);

  if (colorspace) {
    CFRelease(colorspace);
  }
  Buffer dstTempBuffer = {};
  auto dstImageInfo = dstInfo;
  auto dstData = dstPixels;
  if (dstInfo.colorType() != ColorType::RGBA_8888 && dstInfo.colorType() != ColorType::BGRA_8888 &&
      dstInfo.colorType() != ColorType::ALPHA_8) {
    dstImageInfo = info.makeWH(dstInfo.width(), dstInfo.height());
    dstTempBuffer.alloc(dstImageInfo.byteSize());
    dstData = dstTempBuffer.data();
  }
  auto context = CreateBitmapContext(dstImageInfo, dstData);
  auto result = context != nullptr;
  if (result) {
    auto interpolationQuality = GetInterpolationQuality(quality);
    CGContextSetInterpolationQuality(context, interpolationQuality);
    CGContextSetShouldAntialias(context, true);
    int width = static_cast<int>(dstImageInfo.width());
    int height = static_cast<int>(dstImageInfo.height());
    CGRect rect = CGRectMake(0, 0, width, height);
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, rect, image);
    CGContextRelease(context);
  }
  if (!dstTempBuffer.isEmpty()) {
    Pixmap(dstImageInfo, dstData).readPixels(dstInfo, dstPixels);
  }
  return true;
}
}