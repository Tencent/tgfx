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

#include "ScaledImageGenerator.h"
#include "PixelBuffer.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<ScaledImageGenerator> ScaledImageGenerator::MakeFrom(
    const std::shared_ptr<ImageCodec>& codec, int width, int height) {
  if (!codec || width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::shared_ptr<ScaledImageGenerator>(new ScaledImageGenerator(width, height, codec));
}

ScaledImageGenerator::ScaledImageGenerator(int width, int height,
                                           const std::shared_ptr<ImageCodec>& codec)
    : ImageGenerator(width, height, codec->colorSpace()), source(codec) {
}

std::shared_ptr<ImageBuffer> ScaledImageGenerator::onMakeBuffer(bool tryHardware) const {
  auto pixelBuffer = PixelBuffer::Make(width(), height(), isAlphaOnly(), tryHardware, colorSpace());
  if (pixelBuffer == nullptr) {
    return nullptr;
  }
  auto pixels = pixelBuffer->lockPixels();
  auto result = source->readPixels(pixelBuffer->info(), pixels);
  pixelBuffer->unlockPixels();
  return result ? pixelBuffer : nullptr;
}
}  // namespace tgfx