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

#include "tgfx/core/Pixmap.h"
#include "core/PixelRef.h"
#include "core/utils/CopyPixels.h"

namespace tgfx {
Pixmap::Pixmap(const ImageInfo& info, const void* pixels) : _info(info), _pixels(pixels) {
  if (_info.isEmpty() || _pixels == nullptr) {
    _info = {};
    _pixels = nullptr;
  }
}

Pixmap::Pixmap(const ImageInfo& info, void* pixels)
    : _info(info), _pixels(pixels), _writablePixels(pixels) {
  if (_info.isEmpty() || _pixels == nullptr) {
    _info = {};
    _pixels = nullptr;
    _writablePixels = nullptr;
  }
}

Pixmap::Pixmap(const Bitmap& bitmap) {
  reset(bitmap);
}

Pixmap::Pixmap(Bitmap& bitmap) {
  reset(bitmap);
}

Pixmap::~Pixmap() {
  reset();
}

void Pixmap::reset() {
  if (pixelRef != nullptr) {
    pixelRef->unlockPixels();
    pixelRef = nullptr;
  }
  _pixels = nullptr;
  _writablePixels = nullptr;
  _info = {};
}

void Pixmap::reset(const ImageInfo& info, const void* pixels) {
  reset();
  if (!info.isEmpty() && pixels != nullptr) {
    _info = info;
    _pixels = pixels;
  }
}

void Pixmap::reset(const ImageInfo& info, void* pixels) {
  reset();
  if (!info.isEmpty() && pixels != nullptr) {
    _info = info;
    _pixels = pixels;
    _writablePixels = pixels;
  }
}

void Pixmap::reset(const Bitmap& bitmap) {
  reset();
  pixelRef = bitmap.pixelRef;
  _pixels = pixelRef ? pixelRef->lockPixels() : nullptr;
  if (_pixels == nullptr) {
    pixelRef = nullptr;
    return;
  }
  _info = pixelRef->info();
}

void Pixmap::reset(Bitmap& bitmap) {
  reset();
  pixelRef = bitmap.pixelRef;
  _writablePixels = pixelRef ? pixelRef->lockWritablePixels() : nullptr;
  if (_writablePixels == nullptr) {
    pixelRef = nullptr;
    return;
  }
  _pixels = _writablePixels;
  _info = pixelRef->info();
}

Color Pixmap::getColor(int x, int y) const {
  // 使用 RGBA_F16 格式，不会截断超出 [0,1] 范围的值
  auto dstInfo = ImageInfo::Make(1, 1, ColorType::RGBA_F16, AlphaType::Unpremultiplied, 8,
                                 ColorSpace::SRGB());
  uint16_t color[4];  // F16 每通道 2 字节
  if (!readPixels(dstInfo, color, x, y)) {
    return Color::Transparent();
  }
  // Convert IEEE 754-2008 half-precision float to single-precision float
  auto halfToFloat = [](uint16_t h) -> float {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
    uint32_t exponent = static_cast<uint32_t>((h >> 10) & 0x1F);
    uint32_t mantissa = static_cast<uint32_t>(h & 0x3FF);
    uint32_t f = 0;
    if (exponent == 0) {
      // Zero or denormalized number, flush to zero
      f = sign;
    } else if (exponent == 31) {
      // Infinity or NaN
      f = sign | 0x7F800000u | (mantissa << 13);
    } else {
      // Normalized number: convert exponent bias from 15 to 127
      f = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    memcpy(&result, &f, sizeof(float));
    return result;
  };
  return Color(halfToFloat(color[0]), halfToFloat(color[1]),
               halfToFloat(color[2]), halfToFloat(color[3]));
}

Pixmap Pixmap::makeSubset(const Rect& subset) const {
  auto rect = subset;
  rect.round();
  auto bounds = Rect::MakeWH(width(), height());
  if (bounds == rect) {
    return *this;
  }
  if (!bounds.contains(rect)) {
    return {};
  }
  auto srcX = static_cast<int>(rect.x());
  auto srcY = static_cast<int>(rect.y());
  auto width = static_cast<int>(rect.width());
  auto height = static_cast<int>(rect.height());
  auto srcPixels = _info.computeOffset(_pixels, srcX, srcY);
  auto srcInfo = _info.makeWH(width, height);
  return {srcInfo, srcPixels};
}

bool Pixmap::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX, int srcY) const {
  if (_pixels == nullptr || dstPixels == nullptr) {
    return false;
  }
  auto imageInfo = dstInfo.makeIntersect(-srcX, -srcY, _info.width(), _info.height());
  if (imageInfo.isEmpty()) {
    return false;
  }
  auto srcPixels = _info.computeOffset(_pixels, srcX, srcY);
  auto srcInfo = _info.makeWH(imageInfo.width(), imageInfo.height());
  dstPixels = imageInfo.computeOffset(dstPixels, -srcX, -srcY);
  CopyPixels(srcInfo, srcPixels, imageInfo, dstPixels);
  return true;
}

bool Pixmap::writePixels(const ImageInfo& srcInfo, const void* srcPixels, int dstX, int dstY) {
  if (_writablePixels == nullptr || srcPixels == nullptr) {
    return false;
  }
  auto imageInfo = srcInfo.makeIntersect(-dstX, -dstY, _info.width(), _info.height());
  if (imageInfo.isEmpty()) {
    return false;
  }
  srcPixels = imageInfo.computeOffset(srcPixels, -dstX, -dstY);
  auto dstPixels = _info.computeOffset(_writablePixels, dstX, dstY);
  auto dstInfo = _info.makeWH(imageInfo.width(), imageInfo.height());
  CopyPixels(imageInfo, srcPixels, dstInfo, dstPixels);
  return true;
}

bool Pixmap::clear() {
  if (_writablePixels == nullptr) {
    return false;
  }
  if (_info.rowBytes() == _info.minRowBytes()) {
    memset(_writablePixels, 0, byteSize());
  } else {
    auto rowCount = _info.height();
    auto trimRowBytes = static_cast<size_t>(_info.width()) * _info.bytesPerPixel();
    auto pixels = static_cast<char*>(_writablePixels);
    for (int i = 0; i < rowCount; i++) {
      memset(pixels, 0, trimRowBytes);
      pixels += info().rowBytes();
    }
  }
  return true;
}

std::shared_ptr<ColorSpace> Pixmap::colorSpace() const {
  return _info.colorSpace();
}

}  // namespace tgfx
