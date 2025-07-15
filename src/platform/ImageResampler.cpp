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
#ifdef TGFX_USE_STB_IMAGE
#include "platform/ImageResampler.h"
#include "tgfx/core/Buffer.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace tgfx {
static stbir_filter ToStbFilterType(FilterQuality quality) {
  switch (quality) {
    default:
    case FilterQuality::None:
      return stbir_filter::STBIR_FILTER_BOX;
    case FilterQuality::Low:
      return stbir_filter::STBIR_FILTER_TRIANGLE;
    case FilterQuality::Medium:
      return stbir_filter::STBIR_FILTER_CATMULLROM;
    case FilterQuality::High:
      return stbir_filter::STBIR_FILTER_MITCHELL;
  }
}

static void ToStbDataTypeAndChannel(ColorType colorType, stbir_datatype* dataType,
                                    stbir_pixel_layout* pixelLayout) {
  switch (colorType) {
    case ColorType::ALPHA_8:
    case ColorType::Gray_8: {
      *pixelLayout = STBIR_1CHANNEL;
      *dataType = STBIR_TYPE_UINT8;
      break;
    }
    case ColorType::BGRA_8888:
    case ColorType::RGBA_1010102:
    case ColorType::RGBA_F16:
    case ColorType::RGBA_8888: {
      *pixelLayout = STBIR_RGBA;
      *dataType = STBIR_TYPE_UINT8;
      break;
    }
    case ColorType::RGB_565: {
      *pixelLayout = STBIR_RGB;
      *dataType = STBIR_TYPE_UINT8;
      break;
    }
    default: {
      break;
    }
  }
}

bool ImageResamper::Scale(const ImageInfo& srcInfo, const void* srcPixels, const ImageInfo& dstInfo,
                          void* dstPixles, FilterQuality quality) {
  if (srcInfo.isEmpty() || srcPixels == nullptr || dstInfo.isEmpty() || dstPixles == nullptr) {
    return false;
  }
  auto dataType = STBIR_TYPE_UINT8;
  auto channel = STBIR_RGBA;
  ToStbDataTypeAndChannel(srcInfo.colorType(), &dataType, &channel);
  Buffer dstTempBuffer = {};
  Buffer srcTempBuffer = {};
  auto dstImageInfo = dstInfo;
  auto srcImageInfo = srcInfo;
  auto srcData = srcPixels;
  auto dstData = dstPixles;
  if (srcInfo.colorType() == ColorType::RGBA_1010102 ||
      srcInfo.colorType() == ColorType::RGBA_F16) {
    srcImageInfo = srcInfo.makeColorType(ColorType::RGBA_8888);
    srcTempBuffer.alloc(srcImageInfo.byteSize());
    Pixmap(srcInfo, srcPixels).readPixels(srcImageInfo, srcTempBuffer.bytes());
    srcData = srcTempBuffer.data();
  }
  if (srcImageInfo.colorType() != dstInfo.colorType()) {
    dstImageInfo = dstInfo.makeColorType(srcImageInfo.colorType());
    dstTempBuffer.alloc(dstImageInfo.byteSize());
    dstData = dstTempBuffer.data();
  }

  stbir_resize(srcData, srcImageInfo.width(), srcImageInfo.height(), 0, dstData,
               dstImageInfo.width(), dstImageInfo.height(), 0, channel, dataType, STBIR_EDGE_CLAMP,
               ToStbFilterType(quality));

  if (!dstTempBuffer.isEmpty()) {
    Pixmap(dstImageInfo, dstData).readPixels(dstInfo, dstPixles);
  }
  return true;
}

}  // namespace tgfx
#endif