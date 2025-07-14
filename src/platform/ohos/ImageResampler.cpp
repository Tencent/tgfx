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
#include <multimedia/image_framework/image/pixelmap_native.h>
#include "OHOSImageInfo.h"
#include "core/utils/Log.h"
#include "platform/ohos/NativeCodec.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
static OH_PixelmapNative_AntiAliasingLevel ToOHAntiAliasingLevel(FilterQuality quality) {
  switch (quality) {
    default:
    case FilterQuality::None:
      return OH_PixelmapNative_AntiAliasingLevel::OH_PixelmapNative_AntiAliasing_NONE;
    case FilterQuality::Low:
      return OH_PixelmapNative_AntiAliasingLevel::OH_PixelmapNative_AntiAliasing_LOW;
    case FilterQuality::Medium:
      return OH_PixelmapNative_AntiAliasingLevel::OH_PixelmapNative_AntiAliasing_MEDIUM;
    case FilterQuality::High:
      return OH_PixelmapNative_AntiAliasingLevel::OH_PixelmapNative_AntiAliasing_HIGH;
  }
}

bool ImageResamper::Scale(const ImageInfo& srcInfo, const void* srcData, const ImageInfo& dstInfo,
                          void* dstData, FilterQuality quality) {

  if (!srcData) {
    return false;
  }
  OH_Pixelmap_InitializationOptions* options = nullptr;
  auto errorCode = OH_PixelmapInitializationOptions_Create(&options);
  if (errorCode != Image_ErrorCode::IMAGE_SUCCESS) {
    LOGE("ImageResamper::Scale() Failed to Create Initialization Option");
    return false;
  }
  Buffer srcTempBuffer = {};
  auto srcImageInfo = srcInfo;
  auto srcPixels = srcData;
  if (srcInfo.colorType() == ColorType::RGBA_1010102 ||
      srcInfo.colorType() == ColorType::RGBA_F16) {
    srcImageInfo = srcInfo.makeColorType(ColorType::RGBA_8888);
    srcTempBuffer.alloc(srcImageInfo.byteSize());
    Pixmap(srcInfo, srcData).readPixels(srcImageInfo, srcTempBuffer.bytes());
    srcPixels = srcTempBuffer.data();
  }
  OH_PixelmapInitializationOptions_SetWidth(options, static_cast<uint32_t>(srcImageInfo.width()));
  OH_PixelmapInitializationOptions_SetHeight(options, static_cast<uint32_t>(srcImageInfo.height()));
  OH_PixelmapInitializationOptions_SetAlphaType(
      options, OHOSImageInfo::ToOHAlphaType(srcImageInfo.alphaType()));
  OH_PixelmapInitializationOptions_SetPixelFormat(
      options, OHOSImageInfo::ToOHPixelFormat(srcImageInfo.colorType()));
  OH_PixelmapInitializationOptions_SetSrcPixelFormat(
      options, OHOSImageInfo::ToOHPixelFormat(srcImageInfo.colorType()));
  OH_PixelmapNative* pixelMap = nullptr;
  errorCode = OH_PixelmapNative_CreatePixelmap((uint8_t*)srcPixels, srcImageInfo.byteSize(),
                                               options, &pixelMap);
  if (errorCode != Image_ErrorCode::IMAGE_SUCCESS) {
    LOGE("ImageResamper::Scale() Failed to Create PixmapNative");
    return false;
  }
  auto scaleX = static_cast<float>(dstInfo.width()) / static_cast<float>(srcImageInfo.width());
  auto scaleY = static_cast<float>(dstInfo.height()) / static_cast<float>(srcImageInfo.height());
  errorCode = OH_PixelmapNative_ScaleWithAntiAliasing(pixelMap, scaleX, scaleY,
                                                      ToOHAntiAliasingLevel(quality));
  if (errorCode != Image_ErrorCode::IMAGE_SUCCESS) {
    LOGE("ImageResamper::Scale() Failed to Create Scaled Pixmap With Anti Aliasing");
    OH_PixelmapNative_Release(pixelMap);
    OH_PixelmapInitializationOptions_Release(options);
    return false;
  }
  tgfx::Buffer dstTempBuffer = {};
  auto dstImageInfo = dstInfo;
  auto pixels = dstData;
  if (srcInfo.colorType() != dstInfo.colorType()) {
    dstImageInfo = srcImageInfo.makeWH(dstInfo.width(), dstInfo.height());
    dstTempBuffer.alloc(dstImageInfo.byteSize());
    pixels = dstTempBuffer.data();
  }
  auto result = false;
  auto info = NativeCodec::GetPixelmapInfo(pixelMap);
  auto bufferSize = info.byteSize();
  errorCode = OH_PixelmapNative_ReadPixels(pixelMap, static_cast<uint8_t*>(pixels), &bufferSize);
  if (errorCode != Image_ErrorCode::IMAGE_SUCCESS) {
    LOGE("ImageResamper::Scale() PixelmapNative Failed to ReadPixels");
  } else {
    result = true;
  }
  OH_PixelmapNative_Release(pixelMap);
  OH_PixelmapInitializationOptions_Release(options);
  if (!dstTempBuffer.isEmpty()) {
    Pixmap(dstImageInfo, pixels).readPixels(dstInfo, dstData);
  }
  return result;
}
}  // namespace tgfx