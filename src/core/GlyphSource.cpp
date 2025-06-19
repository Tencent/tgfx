/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/GlyphSource.h"

namespace tgfx {

static std::shared_ptr<PixelBuffer> onMakePixelBuffer(const std::shared_ptr<ImageCodec>& imageCodec,
                                                      bool tryHardware) {
  auto pixelBuffer = PixelBuffer::Make(imageCodec->width(), imageCodec->height(),
                                       imageCodec->isAlphaOnly(), tryHardware);
  if (pixelBuffer == nullptr) {
    return nullptr;
  }
  auto pixels = pixelBuffer->lockPixels();
  auto result = imageCodec->readPixels(pixelBuffer->info(), pixels);
  pixelBuffer->unlockPixels();
  return result ? pixelBuffer : nullptr;
}

std::unique_ptr<DataSource<PixelBuffer>> GlyphSource::MakeFrom(
    std::shared_ptr<ImageCodec> imageCodec, bool tryHardware, bool asyncDecoding) {
  if (imageCodec == nullptr) {
    return nullptr;
  }
  if (asyncDecoding && !imageCodec->asyncSupport()) {
    auto pixelBuffer = onMakePixelBuffer(imageCodec, tryHardware);
    if (pixelBuffer == nullptr) {
      return nullptr;
    }
    return Wrap(std::move(pixelBuffer));
  }
  auto glyphSource = std::make_unique<GlyphSource>(std::move(imageCodec), tryHardware);
  if (asyncDecoding) {
    return Async(std::move(glyphSource));
  }
  return glyphSource;
}

GlyphSource::GlyphSource(std::shared_ptr<ImageCodec> imageCodec, bool tryHardware)
    : imageCodec(std::move(imageCodec)), tryHardware(tryHardware) {
}

std::shared_ptr<PixelBuffer> GlyphSource::getData() const {
  return onMakePixelBuffer(imageCodec, tryHardware);
}
}  // namespace tgfx
