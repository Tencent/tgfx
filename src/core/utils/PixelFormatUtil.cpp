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

#include "PixelFormatUtil.h"

namespace tgfx {
PixelFormat ColorTypeToPixelFormat(ColorType type) {
  switch (type) {
    case ColorType::RGBA_8888:
      return PixelFormat::RGBA_8888;
    case ColorType::ALPHA_8:
      return PixelFormat::ALPHA_8;
    case ColorType::BGRA_8888:
      return PixelFormat::BGRA_8888;
    case ColorType::Gray_8:
      return PixelFormat::GRAY_8;
    default:
      return PixelFormat::Unknown;
  }
}

ColorType PixelFormatToColorType(PixelFormat format) {
  switch (format) {
    case PixelFormat::RGBA_8888:
      return ColorType::RGBA_8888;
    case PixelFormat::ALPHA_8:
      return ColorType::ALPHA_8;
    case PixelFormat::BGRA_8888:
      return ColorType::BGRA_8888;
    case PixelFormat::GRAY_8:
      return ColorType::Gray_8;
    default:
      return ColorType::Unknown;
  }
}

size_t PixelFormatBytesPerPixel(PixelFormat format) {
  switch (format) {
    case PixelFormat::ALPHA_8:
    case PixelFormat::GRAY_8:
      return 1;
    case PixelFormat::RG_88:
      return 2;
    case PixelFormat::RGBA_8888:
    case PixelFormat::BGRA_8888:
      return 4;
    default:
      return 0;
  }
}

PixelFormat AtlasFormatToPixelFormat(AtlasFormat format) {
  switch (format) {
    case AtlasFormat::A8:
      return PixelFormat::ALPHA_8;
    case AtlasFormat::RGBA:
      return PixelFormat::RGBA_8888;
    case AtlasFormat::BGRA:
      return PixelFormat::BGRA_8888;
    default:
      return PixelFormat::Unknown;
  }
}

}  // namespace tgfx
