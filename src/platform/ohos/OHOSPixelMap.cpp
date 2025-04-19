/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/platform/ohos/OHOSPixelMap.h"
#include <multimedia/image_framework/image_pixel_map_mdk.h>
#include "OHOSImageInfo.h"
#include "core/utils/Log.h"
#include "tgfx/core/ColorType.h"

namespace tgfx {

ImageInfo OHOSPixelMap::GetInfo(napi_env env, napi_value value) {
  NativePixelMap* pixelmap = OH_PixelMap_InitNativePixelMap(env, value);
  if (pixelmap == nullptr) {
    LOGE("OHOSBitmap::GetInfo() Failed to GetNativePixelMap");
    return {};
  }
  OhosPixelMapInfos info{};
  auto errorCode = OH_PixelMap_GetImageInfo(pixelmap, &info);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("OHOSBitmap::GetInfo() Failed to GetPixelMapInfo");
    return {};
  }
  return ImageInfo::Make(static_cast<int>(info.width), static_cast<int>(info.height),
                         OHOSImageInfo::ToTGFXColorType(info.pixelFormat), AlphaType::Premultiplied,
                         info.rowSize);
}

Bitmap OHOSPixelMap::CopyBitmap(napi_env env, napi_value value) {
  NativePixelMap* pixelmap = OH_PixelMap_InitNativePixelMap(env, value);
  if (pixelmap == nullptr) {
    LOGE("OHOSBitmap::CopyBitmap() Failed to GetNativePixelMap");
    return {};
  }
  OhosPixelMapInfos info{};
  auto errorCode = OH_PixelMap_GetImageInfo(pixelmap, &info);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("OHOSBitmap::CopyBitmap() Failed to GetPixelMapInfo");
    return {};
  }
  auto imageInfo = ImageInfo::Make(static_cast<int>(info.width), static_cast<int>(info.height),
                                   OHOSImageInfo::ToTGFXColorType(info.pixelFormat),
                                   AlphaType::Premultiplied, info.rowSize);

  bool alphaOnly =
      imageInfo.colorType() == ColorType::ALPHA_8 || imageInfo.colorType() == ColorType::Gray_8;

  Bitmap bitmap = {};
  if (!bitmap.allocPixels(imageInfo.width(), imageInfo.height(), alphaOnly)) {
    LOGE("OHOSBitmap::CopyBitmap() Bitmap Failed to allocPixels");
    return {};
  }
  void* pixel;
  errorCode = OH_PixelMap_AccessPixels(pixelmap, &pixel);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("OHOSBitmap::CopyBitmap() OH_PixelMap_AccessPixels failed");
    return {};
  }
  bitmap.writePixels(imageInfo, pixel);
  OH_PixelMap_UnAccessPixels(pixelmap);
  return bitmap;
}
}  // namespace tgfx