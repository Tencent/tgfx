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
#include "core/utils/MathExtra.h"

namespace tgfx {

// RAII lock guard for a PixelRef. lockPixels/lockWritablePixels are called by the factory helpers
// below; unlockPixels is called exactly once when the last shared owner is destroyed. Copies of a
// Pixmap share the same PixelRefLock instance via shared_ptr, so the underlying mutex is unlocked
// exactly once regardless of how many Pixmap copies exist. Defined as a nested class so it stays
// an implementation detail of Pixmap and never appears in the public API surface.
class Pixmap::PixelRefLock {
 public:
  static std::shared_ptr<PixelRefLock> Make(std::shared_ptr<PixelRef> pixelRef,
                                            const void*& pixels) {
    if (pixelRef == nullptr) {
      pixels = nullptr;
      return nullptr;
    }
    pixels = pixelRef->lockPixels();
    if (pixels == nullptr) {
      return nullptr;
    }
    return std::shared_ptr<PixelRefLock>(new PixelRefLock(std::move(pixelRef)));
  }

  static std::shared_ptr<PixelRefLock> MakeWritable(std::shared_ptr<PixelRef> pixelRef,
                                                    void*& writablePixels) {
    if (pixelRef == nullptr) {
      writablePixels = nullptr;
      return nullptr;
    }
    writablePixels = pixelRef->lockWritablePixels();
    if (writablePixels == nullptr) {
      return nullptr;
    }
    return std::shared_ptr<PixelRefLock>(new PixelRefLock(std::move(pixelRef)));
  }

  PixelRefLock(const PixelRefLock&) = delete;
  PixelRefLock& operator=(const PixelRefLock&) = delete;

  ~PixelRefLock() {
    pixelRef->unlockPixels();
  }

  const ImageInfo& info() const {
    return pixelRef->info();
  }

 private:
  explicit PixelRefLock(std::shared_ptr<PixelRef> ref) : pixelRef(std::move(ref)) {
  }

  std::shared_ptr<PixelRef> pixelRef;
};

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

Pixmap::~Pixmap() = default;

void Pixmap::reset() {
  lockGuard = nullptr;
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
  const void* pixels = nullptr;
  lockGuard = PixelRefLock::Make(bitmap.pixelRef, pixels);
  if (lockGuard == nullptr) {
    return;
  }
  _pixels = pixels;
  _info = lockGuard->info();
}

void Pixmap::reset(Bitmap& bitmap) {
  reset();
  void* writablePixels = nullptr;
  lockGuard = PixelRefLock::MakeWritable(bitmap.pixelRef, writablePixels);
  if (lockGuard == nullptr) {
    return;
  }
  _writablePixels = writablePixels;
  _pixels = writablePixels;
  _info = lockGuard->info();
}

RGBA4f<AlphaType::Unpremultiplied> Pixmap::getColor(
    int x, int y, std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto dstInfo = ImageInfo::Make(1, 1, ColorType::RGBA_F16, AlphaType::Unpremultiplied, 8,
                                 std::move(dstColorSpace));
  uint16_t color[4];
  if (!readPixels(dstInfo, color, x, y)) {
    return RGBA4f<AlphaType::Unpremultiplied>::Transparent();
  }
  return {HalfToFloat(color[0]), HalfToFloat(color[1]), HalfToFloat(color[2]),
          HalfToFloat(color[3])};
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
