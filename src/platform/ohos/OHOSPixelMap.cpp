//
// Created on 2024/7/12.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#include "tgfx/platform/ohos/OHOSPixelMap.h"

#include <multimedia/image_framework/image_pixel_map_mdk.h>
#include "OHOSConverter.h"
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
                         OHOSConverter::ToTGFXColorType(info.pixelFormat),
                         hasAlpha == 0 ? AlphaType::Opaque : AlphaType::Premultiplied,
                         info.rowSize);
}

std::shared_ptr<Image> OHOSPixelMap::CopyImage(napi_env env, napi_value value) {
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
                      OHOSConverter::ToTGFXColorType(info.pixelFormat),
                      hasAlpha == 0 ? AlphaType::Opaque : AlphaType::Premultiplied, info.rowSize);
  void* pixel;
  errorCode = OH_PixelMap_AccessPixels(pixelmap, &pixel);
  if (errorCode != IMAGE_RESULT_SUCCESS) {
    LOGE("Could not create ImageCodec, OH_PixelMap_AccessPixels failed");
    return {};
  }
  std::shared_ptr<Data> data = Data::MakeWithCopy(pixel, imageInfo.byteSize());
  OH_PixelMap_UnAccessPixels(pixelmap);
  return Image::MakeFrom(imageInfo, data);
}
}  // namespace tgfx