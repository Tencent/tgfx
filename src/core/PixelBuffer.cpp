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

#include "core/PixelBuffer.h"
#include "core/utils/PixelFormatUtil.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
class RasterPixelBuffer : public PixelBuffer {
 public:
  RasterPixelBuffer(const ImageInfo& info, uint8_t* pixels,
                    std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB())
      : PixelBuffer(info, std::move(colorSpace)), _pixels(pixels) {
  }

  ~RasterPixelBuffer() override {
    delete[] _pixels;
  }

  bool isHardwareBacked() const override {
    return false;
  }

  HardwareBufferRef getHardwareBuffer() const override {
    return nullptr;
  }

 protected:
  void* onLockPixels() const override {
    return _pixels;
  }

  void onUnlockPixels() const override {
  }

  std::shared_ptr<TextureView> onBindToHardwareTexture(Context*) const override {
    return nullptr;
  }

 private:
  uint8_t* _pixels = nullptr;
};

class HardwarePixelBuffer : public PixelBuffer {
 public:
  HardwarePixelBuffer(const ImageInfo& info, HardwareBufferRef hardwareBuffer,
                      std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB())
      : PixelBuffer(info, std::move(colorSpace)),
        hardwareBuffer(HardwareBufferRetain(hardwareBuffer)) {
  }

  ~HardwarePixelBuffer() override {
    HardwareBufferRelease(hardwareBuffer);
  }

  bool isHardwareBacked() const override {
    return HardwareBufferCheck(hardwareBuffer);
  }

  HardwareBufferRef getHardwareBuffer() const override {
    return isHardwareBacked() ? hardwareBuffer : nullptr;
  }

 protected:
  void* onLockPixels() const override {
    return HardwareBufferLock(hardwareBuffer);
  }

  void onUnlockPixels() const override {
    HardwareBufferUnlock(hardwareBuffer);
  }

  std::shared_ptr<TextureView> onBindToHardwareTexture(Context* context) const override {
    return TextureView::MakeFrom(context, hardwareBuffer);
  }

 private:
  HardwareBufferRef hardwareBuffer;
};

std::shared_ptr<PixelBuffer> PixelBuffer::Make(int width, int height, bool alphaOnly,
                                               bool tryHardware,
                                               std::shared_ptr<ColorSpace> colorSpace) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  if (tryHardware) {
    auto hardwareBuffer = HardwareBufferAllocate(width, height, alphaOnly);
    auto pixelBuffer = PixelBuffer::MakeFrom(hardwareBuffer, colorSpace);
    HardwareBufferRelease(hardwareBuffer);
    if (pixelBuffer != nullptr) {
      return pixelBuffer;
    }
  }
  auto colorType = alphaOnly ? ColorType::ALPHA_8 : ColorType::RGBA_8888;
  auto info = ImageInfo::Make(width, height, colorType);
  if (info.isEmpty()) {
    return nullptr;
  }
  auto pixels = new (std::nothrow) uint8_t[info.byteSize()];
  if (pixels == nullptr) {
    return nullptr;
  }
  return std::make_shared<RasterPixelBuffer>(info, pixels, std::move(colorSpace));
}

std::shared_ptr<PixelBuffer> PixelBuffer::MakeFrom(HardwareBufferRef hardwareBuffer,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  return info.isEmpty()
             ? nullptr
             : std::make_shared<HardwarePixelBuffer>(info, hardwareBuffer, std::move(colorSpace));
}

PixelBuffer::PixelBuffer(const ImageInfo& info, std::shared_ptr<ColorSpace> colorSpace)
    : _info(info), _gamutColorSpace(std::move(colorSpace)) {
  if (_info.colorType() == ColorType::ALPHA_8) {
    _gamutColorSpace = nullptr;
  }
}

void* PixelBuffer::lockPixels() {
  locker.lock();
  auto pixels = onLockPixels();
  if (pixels == nullptr) {
    locker.unlock();
  }
  return pixels;
}

void PixelBuffer::unlockPixels() {
  onUnlockPixels();
  locker.unlock();
}

std::shared_ptr<TextureView> PixelBuffer::onMakeTexture(Context* context, bool mipmapped) const {
  std::lock_guard<std::mutex> autoLock(locker);
  if (!mipmapped && isHardwareBacked()) {
    auto result = onBindToHardwareTexture(context);
    if (result) {
      result->setGamutColorSpace(_gamutColorSpace);
    }
    return result;
  }
  auto pixels = onLockPixels();
  if (pixels == nullptr) {
    return nullptr;
  }
  auto format = ColorTypeToPixelFormat(_info.colorType());
  auto textureView =
      TextureView::MakeFormat(context, width(), height(), pixels, _info.rowBytes(), format,
                              mipmapped, ImageOrigin::TopLeft, _gamutColorSpace);
  onUnlockPixels();
  return textureView;
}
}  // namespace tgfx
