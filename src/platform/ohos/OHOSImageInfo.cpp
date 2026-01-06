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

#include "OHOSImageInfo.h"
#include <multimedia/image_framework/image/pixelmap_native.h>
#include <multimedia/image_framework/image_pixel_map_mdk.h>

namespace tgfx {

Orientation OHOSImageInfo::ToTGFXOrientation(const char* value, size_t size) {
  std::string orientationStr = std::string(value, size);
  static const std::unordered_map<std::string, Orientation> OrientationMap = {
      {"Top-left", Orientation::TopLeft},         {"Top-right", Orientation::TopRight},
      {"Bottom-right", Orientation::BottomRight}, {"Bottom-left", Orientation::BottomLeft},
      {"Left-top", Orientation::LeftTop},         {"Right-top", Orientation::RightTop},
      {"Right-bottom", Orientation::RightBottom}, {"Left-bottom", Orientation::LeftBottom}};
  if (OrientationMap.find(orientationStr) != OrientationMap.end()) {
    return OrientationMap.at(orientationStr);
  }
  return Orientation::TopLeft;
}

ColorType OHOSImageInfo::ToTGFXColorType(int ohPixelFormat) {
  switch (ohPixelFormat) {
    case PIXEL_FORMAT_RGBA_8888:
      return ColorType::RGBA_8888;
    case PIXEL_FORMAT_BGRA_8888:
      return ColorType::BGRA_8888;
    case PIXEL_FORMAT_ALPHA_8:
      return ColorType::ALPHA_8;
    case PIXEL_FORMAT_RGBA_F16:
      return ColorType::RGBA_F16;
    case PIXEL_FORMAT_RGB_565:
      return ColorType::RGB_565;
  }
  return ColorType::Unknown;
}

AlphaType OHOSImageInfo::ToTGFXAlphaType(int ohAlphaType) {
  switch (ohAlphaType) {
    case OHOS_PIXEL_MAP_ALPHA_TYPE_UNPREMUL:
      return AlphaType::Unpremultiplied;
    case OHOS_PIXEL_MAP_ALPHA_TYPE_PREMUL:
      return AlphaType::Premultiplied;
    case OHOS_PIXEL_MAP_ALPHA_TYPE_OPAQUE:
      return AlphaType::Opaque;
    case OHOS_PIXEL_MAP_ALPHA_TYPE_UNKNOWN:
      return AlphaType::Unknown;
  }
  return AlphaType::Unknown;
}

int OHOSImageInfo::ToOHPixelFormat(ColorType colorType) {
  switch (colorType) {
    case ColorType::RGBA_8888:
      return PIXEL_FORMAT_RGBA_8888;
    case ColorType::BGRA_8888:
      return PIXEL_FORMAT_BGRA_8888;
    case ColorType::ALPHA_8:
    case ColorType::Gray_8:
      return PIXEL_FORMAT_ALPHA_8;
    case ColorType::RGBA_F16:
      return PIXEL_FORMAT_RGBA_F16;
    case ColorType::RGB_565:
      return PIXEL_FORMAT_RGB_565;
    default:
      return PIXEL_FORMAT_UNKNOWN;
  }
}
}  // namespace tgfx
