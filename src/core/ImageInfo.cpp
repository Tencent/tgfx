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

#include "tgfx/core/ImageInfo.h"

namespace tgfx {
static constexpr int MaxDimension = INT32_MAX >> 2;

bool ImageInfo::IsValidSize(int width, int height) {
  return width > 0 && width <= MaxDimension && height > 0 && height <= MaxDimension;
}

size_t ImageInfo::GetBytesPerPixel(ColorType colorType) {
  switch (colorType) {
    case ColorType::ALPHA_8:
    case ColorType::Gray_8:
      return 1;
    case ColorType::RGB_565:
      return 2;
    case ColorType::RGBA_8888:
    case ColorType::BGRA_8888:
    case ColorType::RGBA_1010102:
      return 4;
    case ColorType::RGBA_F16:
      return 8;
    default:
      return 0;
  }
}

ImageInfo ImageInfo::Make(int width, int height, ColorType colorType, AlphaType alphaType,
                          size_t rowBytes, std::shared_ptr<ColorSpace> colorSpace) {
  static ImageInfo emptyInfo = {};
  if (colorType == ColorType::Unknown || alphaType == AlphaType::Unknown ||
      !IsValidSize(width, height)) {
    return emptyInfo;
  }
  auto bytesPerPixel = GetBytesPerPixel(colorType);
  if (rowBytes > 0) {
    if (rowBytes < static_cast<size_t>(width) * bytesPerPixel) {
      return emptyInfo;
    }
  } else {
    rowBytes = static_cast<size_t>(width) * bytesPerPixel;
  }
  if (colorType == ColorType::ALPHA_8) {
    alphaType = AlphaType::Premultiplied;
    colorSpace = nullptr;
  } else if (colorType == ColorType::RGB_565) {
    alphaType = AlphaType::Opaque;
  }
  return {width, height, colorType, alphaType, rowBytes, std::move(colorSpace)};
}

size_t ImageInfo::bytesPerPixel() const {
  return GetBytesPerPixel(_colorType);
}

ImageInfo ImageInfo::makeIntersect(int x, int y, int targetWidth, int targetHeight) const {
  int left = std::max(0, x);
  int right = std::min(_width, x + targetWidth);
  int top = std::max(0, y);
  int bottom = std::min(_height, y + targetHeight);
  return makeWH(right - left, bottom - top);
}

static int Clamp(int value, int max) {
  if (value < 0) {
    value = 0;
  }
  if (value > max) {
    value = max;
  }
  return value;
}

const void* ImageInfo::computeOffset(const void* pixels, int x, int y) const {
  if (pixels == nullptr || isEmpty()) {
    return pixels;
  }
  x = Clamp(x, _width - 1);
  y = Clamp(y, _height - 1);
  auto offset = static_cast<size_t>(y) * _rowBytes + static_cast<size_t>(x) * bytesPerPixel();
  return reinterpret_cast<const uint8_t*>(pixels) + offset;
}

void* ImageInfo::computeOffset(void* pixels, int x, int y) const {
  if (pixels == nullptr || isEmpty()) {
    return pixels;
  }
  x = Clamp(x, _width - 1);
  y = Clamp(y, _height - 1);
  auto offset = static_cast<size_t>(y) * _rowBytes + static_cast<size_t>(x) * bytesPerPixel();
  return reinterpret_cast<uint8_t*>(pixels) + offset;
}

bool operator==(const ImageInfo& a, const ImageInfo& b) {
  return a._width == b._width && a._height == b._height && a._colorType == b._colorType &&
         a._alphaType == b._alphaType && a._rowBytes == b._rowBytes &&
         ColorSpace::Equals(a._colorSpace.get(), b._colorSpace.get());
}

}  // namespace tgfx
