/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "NativeCodec.h"
#include <multimedia/image_framework/image/image_source_native.h>
#include <cstdint>
#include "OHOSImageInfo.h"
#include "core/utils/Log.h"
#include "tgfx/core/AlphaType.h"
#include "tgfx/core/ColorType.h"
#include "tgfx/core/Image.h"

namespace tgfx {

std::shared_ptr<ImageCodec> ImageCodec::MakeFrom(NativeImageRef) {
  return nullptr;
}

Orientation GetOrientation(OH_ImageSourceNative* imageSource) {
  char targetData[] = "Orientation";
  Image_String target;
  target.size = strlen(targetData);
  target.data = targetData;

  Image_String response{};

  auto errorCode = OH_ImageSourceNative_GetImageProperty(imageSource, &target, &response);
  if (errorCode == IMAGE_SUCCESS) {
    auto result = OHOSImageInfo::ToTGFXOrientation(response.data, response.size);
    free(response.data);
    return result;
  }
  return Orientation::TopLeft;
}

std::shared_ptr<ImageCodec> ImageCodec::MakeNativeCodec(const std::string& filePath) {
  OH_ImageSourceNative* imageSource = nullptr;
  Image_ErrorCode errorCode = OH_ImageSourceNative_CreateFromUri(
      const_cast<char*>(filePath.c_str()), static_cast<size_t>(filePath.length()), &imageSource);
  if (errorCode != IMAGE_SUCCESS) {
    LOGE("NativeCodec::CreateImageSource() Failed to CreateFromUri: %s", filePath.c_str());
    return nullptr;
  }
  auto result = NativeCodec::Make(imageSource);
  result->imagePath = filePath;
  OH_ImageSourceNative_Release(imageSource);
  return result;
}

std::shared_ptr<ImageCodec> ImageCodec::MakeNativeCodec(std::shared_ptr<Data> imageBytes) {
  OH_ImageSourceNative* imageSource = nullptr;
  Image_ErrorCode errorCode = OH_ImageSourceNative_CreateFromData(
      const_cast<uint8_t*>(imageBytes->bytes()), imageBytes->size(), &imageSource);
  if (errorCode != IMAGE_SUCCESS) {
    LOGE("Could not create ImageCodec, OH_ImageSourceNative_CreateFromData failed");
    return nullptr;
  }
  auto result = NativeCodec::Make(imageSource);
  result->imageBytes = imageBytes;
  OH_ImageSourceNative_Release(imageSource);
  return result;
}

bool NativeCodec::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                               std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const {
  auto image = CreateImageSource();
  if (!image) {
    return false;
  }
  OH_DecodingOptions* options;
  auto errorCode = OH_DecodingOptions_Create(&options);
  if (errorCode != Image_ErrorCode::IMAGE_SUCCESS) {
    LOGE("NativeCodec::readPixels() Failed to Create Decode Option");
    OH_ImageSourceNative_Release(image);
    return false;
  }
  OH_DecodingOptions_SetPixelFormat(options, OHOSImageInfo::ToOHPixelFormat(colorType));
  // decode
  OH_PixelmapNative* pixelmap = nullptr;
  errorCode = OH_ImageSourceNative_CreatePixelmap(image, options, &pixelmap);
  if (errorCode != IMAGE_SUCCESS) {
    OH_ImageSourceNative_Release(image);
    LOGE("NativeCodec::readPixels() Failed to Decode Image");
    return false;
  }
  auto info = GetPixelmapInfo(pixelmap);
  auto dstInfo =
      ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes, dstColorSpace);
  bool result = false;
  if (info == dstInfo) {
    size_t bufferSize = info.byteSize();
    errorCode =
        OH_PixelmapNative_ReadPixels(pixelmap, static_cast<uint8_t*>(dstPixels), &bufferSize);
    if (errorCode != IMAGE_SUCCESS) {
      LOGE("NativeCodec::readPixels() PixelmapNative Failed to ReadPixels");
    } else {
      result = true;
    }
  } else {
    uint8_t* pixels = new (std::nothrow) uint8_t[info.byteSize()];
    if (pixels) {
      size_t bufferSize = info.byteSize();
      errorCode = OH_PixelmapNative_ReadPixels(pixelmap, pixels, &bufferSize);
      if (errorCode != IMAGE_SUCCESS) {
        LOGE("NativeCodec::readPixels() PixelmapNative Failed to ReadPixels");
      } else {
        result = Pixmap(info, pixels).readPixels(dstInfo, dstPixels);
      }
      delete[] pixels;
    }
  }
  OH_ImageSourceNative_Release(image);
  OH_PixelmapNative_Release(pixelmap);
  return result;
}

std::shared_ptr<NativeCodec> NativeCodec::Make(OH_ImageSourceNative* imageSource) {
  if (imageSource == nullptr) {
    return nullptr;
  }
  OH_ImageSource_Info* info = nullptr;
  Image_ErrorCode errorCode = OH_ImageSourceInfo_Create(&info);
  if (errorCode != IMAGE_SUCCESS) {
    LOGE("Could not create ImageCodec, OH_ImageSourceInfo_Create failed");
    return nullptr;
  }
  errorCode = OH_ImageSourceNative_GetImageInfo(imageSource, 0, info);
  if (errorCode != IMAGE_SUCCESS) {
    OH_ImageSourceInfo_Release(info);
    LOGE("Could not create ImageCodec, OH_ImageSourceNative_GetImageInfo failed");
    return nullptr;
  }
  Orientation orientation = GetOrientation(imageSource);

  uint32_t width = 0;
  errorCode = OH_ImageSourceInfo_GetWidth(info, &width);
  if (errorCode != IMAGE_SUCCESS) {
    OH_ImageSourceInfo_Release(info);
    LOGE("Could not create ImageCodec, OH_ImageSourceInfo_GetWidth failed");
    return nullptr;
  }

  uint32_t height = 0;
  errorCode = OH_ImageSourceInfo_GetHeight(info, &height);
  if (errorCode != IMAGE_SUCCESS) {
    OH_ImageSourceInfo_Release(info);
    LOGE("Could not create ImageCodec, OH_ImageSourceInfo_GetHeight failed");
    return nullptr;
  }

  OH_ImageSourceInfo_Release(info);
  return std::shared_ptr<NativeCodec>(
      new NativeCodec(static_cast<int>(width), static_cast<int>(height), orientation));
}

ImageInfo NativeCodec::GetPixelmapInfo(OH_PixelmapNative* pixelmap) {
  OH_Pixelmap_ImageInfo* currentInfo = nullptr;
  auto errorCode = OH_PixelmapImageInfo_Create(&currentInfo);
  if (errorCode != Image_ErrorCode::IMAGE_SUCCESS) {
    LOGE("NativeCodec::readPixels() Failed to Decode Image");
    return {};
  }
  OH_PixelmapNative_GetImageInfo(pixelmap, currentInfo);

  int32_t pixelFormat = 0;
  OH_PixelmapImageInfo_GetPixelFormat(currentInfo, &pixelFormat);
  ColorType colorType = OHOSImageInfo::ToTGFXColorType(pixelFormat);

  int32_t alpha = 0;
  OH_PixelmapImageInfo_GetAlphaType(currentInfo, &alpha);
  AlphaType alphaType = OHOSImageInfo::ToTGFXAlphaType(alpha);
  if (alphaType == AlphaType::Unknown) {
    // Default is Premultiplied
    alphaType = AlphaType::Premultiplied;
  }
  uint32_t width = 0;
  OH_PixelmapImageInfo_GetWidth(currentInfo, &width);
  uint32_t height = 0;
  OH_PixelmapImageInfo_GetHeight(currentInfo, &height);
  uint32_t rowBytes = 0;
  OH_PixelmapImageInfo_GetRowStride(currentInfo, &rowBytes);
  OH_PixelmapImageInfo_Release(currentInfo);
  return ImageInfo::Make((int)width, (int)height, colorType, alphaType, rowBytes,
                         ColorSpace::SRGB());
}

OH_ImageSourceNative* NativeCodec::CreateImageSource() const {
  OH_ImageSourceNative* imageSource = nullptr;
  if (imagePath.empty()) {
    auto errorCode = OH_ImageSourceNative_CreateFromData(const_cast<uint8_t*>(imageBytes->bytes()),
                                                         imageBytes->size(), &imageSource);
    if (errorCode != IMAGE_SUCCESS) {
      LOGE("NativeCodec::CreateImageSource() Failed to CreateFromData");
      return nullptr;
    }
  } else {
    auto errorCode =
        OH_ImageSourceNative_CreateFromUri(const_cast<char*>(imagePath.c_str()),
                                           static_cast<size_t>(imagePath.length()), &imageSource);
    if (errorCode != IMAGE_SUCCESS) {
      LOGE("NativeCodec::CreateImageSource() Failed to CreateFromUri: %s", imagePath.c_str());
      return nullptr;
    }
  }
  return imageSource;
}

}  // namespace tgfx