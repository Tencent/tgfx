//
// Created on 2024/7/12.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#include "tgfx/platform/ohos/OHOSPixelMap.h"
#include <multimedia/image_framework/image_pixel_map_mdk.h>
#include "OHOSImageInfo.h"
#include "tgfx/core/ColorType.h"
#include "utils/Log.h"

namespace tgfx {

ImageInfo OHOSPixelMap::GetInfo(napi_env env, napi_value value) {
  NativePixelMap* pixelmap = OH_PixelMap_InitNativePixelMap(env, value);
  if (pixelmap == nullptr) {
    LOGE("OHOSBitmap::GetInfo() Failed to GetNativePixelMap");
    return {};
  }
  int32_t hasAlpha = 0;
  auto errorCode = OH_PixelMap_IsSupportAlpha(pixelmap, &hasAlpha);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("OHOSBitmap::GetInfo() Failed to GetAlphaInfo");
    return {};
  }
  OhosPixelMapInfos info{};
  errorCode = OH_PixelMap_GetImageInfo(pixelmap, &info);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("OHOSBitmap::GetInfo() Failed to GetPixelMapInfo");
    return {};
  }
  return ImageInfo::Make(static_cast<int>(info.width), static_cast<int>(info.height),
                         OHOSImageInfo::ToTGFXColorType(info.pixelFormat),
                         hasAlpha == 0 ? AlphaType::Opaque : AlphaType::Premultiplied,
                         info.rowSize);
}

std::shared_ptr<Bitmap> OHOSPixelMap::CopyBitmap(napi_env env, napi_value value) {
  NativePixelMap* pixelmap = OH_PixelMap_InitNativePixelMap(env, value);
  if (pixelmap == nullptr) {
    LOGE("OHOSBitmap::CopyBitmap() Failed to GetNativePixelMap");
    return nullptr;
  }
  int32_t hasAlpha = 0;
  auto errorCode = OH_PixelMap_IsSupportAlpha(pixelmap, &hasAlpha);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("OHOSBitmap::CopyBitmap() Failed to GetAlphaInfo");
    return nullptr;
  }
  OhosPixelMapInfos info{};
  errorCode = OH_PixelMap_GetImageInfo(pixelmap, &info);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("OHOSBitmap::GetInfo() Failed to GetPixelMapInfo");
    return nullptr;
  }
  auto imageInfo =
      ImageInfo::Make(static_cast<int>(info.width), static_cast<int>(info.height),
                      OHOSImageInfo::ToTGFXColorType(info.pixelFormat),
                      hasAlpha == 0 ? AlphaType::Opaque : AlphaType::Premultiplied, info.rowSize);
  void* pixel;
  errorCode = OH_PixelMap_AccessPixels(pixelmap, &pixel);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("Could not create ImageCodec, OH_PixelMap_AccessPixels failed");
    return {};
  }
  bool alphaOnly =
      imageInfo.colorType() == ColorType::ALPHA_8 || imageInfo.colorType() == ColorType::Gray_8;
  auto bitmap = std::make_shared<Bitmap>(imageInfo.width(), imageInfo.height(), alphaOnly);
  bitmap->writePixels(imageInfo, pixel);
  OH_PixelMap_UnAccessPixels(pixelmap);
  return bitmap;
}
}  // namespace tgfx