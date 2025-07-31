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

#include "ScaledImageBuffer.h"
#include "ImageResize.h"
#include "ImageResize2.h"
#include "PixelBuffer.h"

namespace tgfx {
std::shared_ptr<ScaledImageBuffer> ScaledImageBuffer::Make(
    int width, int height, const std::shared_ptr<ImageBuffer>& source) {
  if (!source || width <= 0 || height <= 0) {
    return nullptr;
  }
  auto image = std::shared_ptr<ScaledImageBuffer>(
      new ScaledImageBuffer(width, height, source));
  return image;
}

ScaledImageBuffer::ScaledImageBuffer(int width, int height,
                                     const std::shared_ptr<ImageBuffer>& source): _width(width), _height(height), source(source) {
}

std::shared_ptr<Texture> ScaledImageBuffer::onMakeTexture(Context* context, bool mipmapped) const {
  if (!source->isPixelBuffer() || (width() == source->width() && height() == source->height())) {
    return source->onMakeTexture(context, mipmapped);
  }
  auto pixelBuffer = std::dynamic_pointer_cast<PixelBuffer>(source);
  auto scaledPixelBuffer = PixelBuffer::Make(width(), height(), pixelBuffer->isAlphaOnly(), pixelBuffer->isHardwareBacked());
  auto pixels = scaledPixelBuffer->lockPixels();
  auto srcPixels = pixelBuffer->lockPixels();
  ImageResize2(srcPixels, pixelBuffer->info(), pixels, scaledPixelBuffer->info());
  scaledPixelBuffer->unlockPixels();
  pixelBuffer->unlockPixels();

  auto imageBuffer  = static_cast<std::shared_ptr<ImageBuffer>>(scaledPixelBuffer);
  return imageBuffer->onMakeTexture(context, mipmapped);
}
}