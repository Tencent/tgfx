/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "core/utils/TextureGrainNoise.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace tgfx {

namespace {

constexpr float TwoPi = 6.28318530717958647692f;

uint32_t MixBits(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}

uint32_t CellHash(int x, int y, int channel) {
  auto ux = static_cast<uint32_t>(x);
  auto uy = static_cast<uint32_t>(y);
  auto salt = static_cast<uint32_t>(channel) * 0x9e3779b9U;
  return MixBits(ux + MixBits(uy + salt));
}

uint8_t EncodeSigned(float value) {
  value = std::clamp(value, -1.0f, 1.0f);
  return static_cast<uint8_t>(std::lround((value * 0.5f + 0.5f) * 255.0f));
}

float DecodeSigned(uint8_t value) {
  return static_cast<float>(value) * (2.0f / 255.0f) - 1.0f;
}

int WrapCell(int value) {
  value %= TextureGrainTableSize;
  if (value < 0) {
    value += TextureGrainTableSize;
  }
  return value;
}

std::array<Point, 2> GradientAt(const uint8_t* table, int x, int y) {
  x = WrapCell(x);
  y = WrapCell(y);
  auto offset = (static_cast<size_t>(y) * static_cast<size_t>(TextureGrainTableSize) +
                 static_cast<size_t>(x)) *
                4;
  return {Point::Make(DecodeSigned(table[offset]), DecodeSigned(table[offset + 1])),
          Point::Make(DecodeSigned(table[offset + 2]), DecodeSigned(table[offset + 3]))};
}

Point DotGradient(const std::array<Point, 2>& gradient, float x, float y) {
  return Point::Make(Point::DotProduct(gradient[0], Point::Make(x, y)),
                     Point::DotProduct(gradient[1], Point::Make(x, y)));
}

Point Mix(Point first, Point second, float amount) {
  return first + (second - first) * amount;
}

Point GradientNoise(const uint8_t* table, float x, float y, int tableOffsetX, int tableOffsetY) {
  auto baseX = static_cast<int>(std::floor(x));
  auto baseY = static_cast<int>(std::floor(y));
  auto fractionX = x - static_cast<float>(baseX);
  auto fractionY = y - static_cast<float>(baseY);
  auto smoothX = fractionX * fractionX * (3.0f - 2.0f * fractionX);
  auto smoothY = fractionY * fractionY * (3.0f - 2.0f * fractionY);

  auto x0 = baseX + tableOffsetX;
  auto y0 = baseY + tableOffsetY;
  auto x1 = x0 + 1;
  auto y1 = y0 + 1;
  auto g00 = GradientAt(table, x0, y0);
  auto g10 = GradientAt(table, x1, y0);
  auto g01 = GradientAt(table, x0, y1);
  auto g11 = GradientAt(table, x1, y1);

  auto bottomLeft = DotGradient(g00, fractionX, fractionY);
  auto bottomRight = DotGradient(g10, fractionX - 1.0f, fractionY);
  auto topLeft = DotGradient(g01, fractionX, fractionY - 1.0f);
  auto topRight = DotGradient(g11, fractionX - 1.0f, fractionY - 1.0f);
  auto bottom = Mix(bottomLeft, bottomRight, smoothX);
  auto top = Mix(topLeft, topRight, smoothX);
  return Mix(bottom, top, smoothY);
}

}  // namespace

static std::vector<uint8_t> BuildTextureGrainGradientTable() {
  constexpr int tableSize = TextureGrainTableSize;
  std::vector<uint8_t> table(static_cast<size_t>(tableSize) * static_cast<size_t>(tableSize) * 4);
  for (int y = 0; y < tableSize; ++y) {
    for (int x = 0; x < tableSize; ++x) {
      auto offset =
          (static_cast<size_t>(y) * static_cast<size_t>(tableSize) + static_cast<size_t>(x)) * 4;
      for (int channel = 0; channel < 2; ++channel) {
        auto hash = CellHash(x, y, channel);
        auto angle = (static_cast<float>(hash & 0x3ffU) + 0.5f) * (TwoPi / 1024.0f);
        table[offset + static_cast<size_t>(channel) * 2] = EncodeSigned(std::cos(angle));
        table[offset + static_cast<size_t>(channel) * 2 + 1] = EncodeSigned(std::sin(angle));
      }
    }
  }
  return table;
}

const std::vector<uint8_t>& TextureGrainGradientTable() {
  static const std::vector<uint8_t> table = BuildTextureGrainGradientTable();
  return table;
}

Point TextureGrainSeedOffset(uint32_t seed, int octave) {
  auto hash = MixBits(seed + static_cast<uint32_t>(octave + 1) * 0x85ebca6bU);
  auto x = static_cast<int>(hash & 0xffU);
  auto y = static_cast<int>((hash >> 8) & 0xffU);
  return Point::Make(static_cast<float>(x), static_cast<float>(y));
}

Point TextureGrainSignedFBM(const uint8_t* gradientTable, float x, float y, uint32_t seed) {
  if (gradientTable == nullptr) {
    return Point::Zero();
  }
  Point result = Point::Zero();
  float weight = 1.0f;
  float scale = 1.0f;
  for (int octave = 0; octave < 3; ++octave) {
    auto offset = TextureGrainSeedOffset(seed, octave);
    auto offsetX = static_cast<int>(offset.x);
    auto offsetY = static_cast<int>(offset.y);
    auto noise = GradientNoise(gradientTable, x * scale, y * scale, offsetX, offsetY);
    result += noise * weight;
    scale *= 2.0f;
    weight *= 0.5f;
  }
  return result;
}

}  // namespace tgfx
