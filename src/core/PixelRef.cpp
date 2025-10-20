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

#include "PixelRef.h"

namespace tgfx {
std::shared_ptr<PixelRef> PixelRef::Make(int width, int height, bool alphaOnly, bool tryHardware,
                                         std::shared_ptr<ColorSpace> colorSpace) {
  auto pixelBuffer =
      PixelBuffer::Make(width, height, alphaOnly, tryHardware, std::move(colorSpace));
  return Wrap(std::move(pixelBuffer));
}

std::shared_ptr<PixelRef> PixelRef::Wrap(std::shared_ptr<PixelBuffer> pixelBuffer) {
  if (pixelBuffer == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<PixelRef>(new PixelRef((std::move(pixelBuffer))));
}

PixelRef::PixelRef(std::shared_ptr<PixelBuffer> pixelBuffer) : pixelBuffer(std::move(pixelBuffer)) {
}

void PixelRef::setGamutColorSpace(std::shared_ptr<ColorSpace> colorSpace) {
  auto pixels = pixelBuffer->lockPixels();
  if (pixels == nullptr) {
    return;
  }
  if (pixelBuffer.use_count() != 1) {
    auto& info = pixelBuffer->info();
    auto newBuffer = PixelBuffer::Make(info.width(), info.height(), info.isAlphaOnly(),
                                       pixelBuffer->isHardwareBacked());
    if (newBuffer == nullptr) {
      pixelBuffer->unlockPixels();
      return;
    }
    auto dstPixels = newBuffer->lockPixels();
    memcpy(dstPixels, pixels, info.byteSize());
    pixelBuffer->unlockPixels();
    pixelBuffer = newBuffer;
  }
  pixelBuffer->setGamutColorSpace(std::move(colorSpace));
}

void* PixelRef::lockWritablePixels() {
  auto pixels = pixelBuffer->lockPixels();
  if (pixels == nullptr) {
    return nullptr;
  }
  if (pixelBuffer.use_count() != 1) {
    auto& info = pixelBuffer->info();
    auto newBuffer =
        PixelBuffer::Make(info.width(), info.height(), info.isAlphaOnly(),
                          pixelBuffer->isHardwareBacked(), pixelBuffer->gamutColorSpace());
    if (newBuffer == nullptr) {
      pixelBuffer->unlockPixels();
      return nullptr;
    }
    auto dstPixels = newBuffer->lockPixels();
    memcpy(dstPixels, pixels, info.byteSize());
    pixelBuffer->unlockPixels();
    pixels = dstPixels;
    pixelBuffer = newBuffer;
  }
  return pixels;
}

void PixelRef::clear() {
  auto pixels = lockWritablePixels();
  if (pixels != nullptr) {
    memset(pixels, 0, info().byteSize());
  }
  unlockPixels();
}
}  // namespace tgfx
