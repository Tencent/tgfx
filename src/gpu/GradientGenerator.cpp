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

#include "GradientGenerator.h"
#include "core/PixelBuffer.h"

namespace tgfx {
static constexpr int GradientWidth = 256;

GradientGenerator::GradientGenerator(const Color* colors, const float* positions, int count)
    : ImageGenerator(GradientWidth, 1), colors(colors, colors + count),
      positions(positions, positions + count) {
}

std::shared_ptr<ImageBuffer> GradientGenerator::onMakeBuffer(bool) const {
  auto pixelBuffer = PixelBuffer::Make(GradientWidth, 1, false, false);
  if (pixelBuffer == nullptr) {
    return nullptr;
  }
  auto pixels = reinterpret_cast<uint8_t*>(pixelBuffer->lockPixels());
  if (pixels == nullptr) {
    return nullptr;
  }
  memset(pixels, 0, pixelBuffer->info().byteSize());
  int prevIndex = 0;
  auto count = colors.size();
  for (size_t i = 1; i < count; ++i) {
    int nextIndex = std::min(static_cast<int>(positions[i] * static_cast<float>(GradientWidth)),
                             GradientWidth - 1);

    if (nextIndex > prevIndex) {
      auto r0 = colors[i - 1].red;
      auto g0 = colors[i - 1].green;
      auto b0 = colors[i - 1].blue;
      auto a0 = colors[i - 1].alpha;
      auto r1 = colors[i].red;
      auto g1 = colors[i].green;
      auto b1 = colors[i].blue;
      auto a1 = colors[i].alpha;

      auto step = 1.0f / static_cast<float>(nextIndex - prevIndex);
      auto deltaR = (r1 - r0) * step;
      auto deltaG = (g1 - g0) * step;
      auto deltaB = (b1 - b0) * step;
      auto deltaA = (a1 - a0) * step;

      for (int curIndex = prevIndex; curIndex <= nextIndex; ++curIndex) {
        pixels[curIndex * 4] = static_cast<uint8_t>(r0 * 255.0f);
        pixels[curIndex * 4 + 1] = static_cast<uint8_t>(g0 * 255.0f);
        pixels[curIndex * 4 + 2] = static_cast<uint8_t>(b0 * 255.0f);
        pixels[curIndex * 4 + 3] = static_cast<uint8_t>(a0 * 255.0f);
        r0 += deltaR;
        g0 += deltaG;
        b0 += deltaB;
        a0 += deltaA;
      }
    }
    prevIndex = nextIndex;
  }
  pixelBuffer->unlockPixels();
  return pixelBuffer;
}
}  // namespace tgfx
