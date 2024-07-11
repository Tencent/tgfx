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

#include "NativeImageCodec.h"
#include <multimedia/image_framework/image_pixel_map_mdk.h>
#include <multimedia/image_framework/image_source_mdk.h>
#include "tgfx/core/Image.h"
#include "tgfx/platform/ohos/HarmonyImage.h"
#include "utils/Log.h"

namespace tgfx {

std::shared_ptr<ImageCodec> ImageCodec::MakeFrom(NativeImageRef nativeImage) {
  if (nativeImage == nullptr) {
    return nullptr;
  }
  OhosPixelMapInfos info{};
  auto errorCode = OH_PixelMap_GetImageInfo(nativeImage->pixelMap, &info);
  ImageInfo imageInfo{};
  if (errorCode == IMAGE_RESULT_SUCCESS) {
    imageInfo = ImageInfo::Make(static_cast<int>(info.width), static_cast<int>(info.height),
                                HarmonyImage::ToTGFXColorType(info.pixelFormat),
                                nativeImage->alphaType, info.rowSize);
  } else {
    LOGE("ImageCodec::MakeFrom() Failed to GetPixelMapInfo");
    return nullptr;
  }

  int32_t rowBytes = 0;
  OH_PixelMap_GetBytesNumberPerRow(nativeImage->pixelMap, &rowBytes);

  void* pixel;
  errorCode = OH_PixelMap_AccessPixels(nativeImage->pixelMap, &pixel);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("Could not create ImageCodec, OH_PixelMap_AccessPixels failed");
    return nullptr;
  }
  std::shared_ptr<Data> data = Data::MakeWithCopy(pixel, imageInfo.byteSize());
  OH_PixelMap_UnAccessPixels(nativeImage->pixelMap);

  return std::shared_ptr<NativeImageCodec>(new NativeImageCodec(
      imageInfo.width(), imageInfo.height(), nativeImage->orientation, data, imageInfo));
}

bool NativeImageCodec::readPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  return Pixmap(imageInfo, imageData->bytes()).readPixels(dstInfo, dstPixels);
}

}  // namespace tgfx