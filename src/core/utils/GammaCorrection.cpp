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
#include "GammaCorrection.h"
#include <cmath>

namespace tgfx {
static float LinearToSRGB(float linear) {
  // The magic numbers are derived from the sRGB specification.
  // See http://www.color.org/chardata/rgb/srgb.xalter.
  if (linear <= 0.0031308f) {
    return linear * 12.92f;
  }
  return 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
}

const std::array<uint8_t, 256>& GammaCorrection::GammaTable() {
  static const std::array<uint8_t, 256> table = [] {
    std::array<uint8_t, 256> table{};
    table[0] = 0;
    table[255] = 255;
    for (size_t i = 1; i < 255; ++i) {
      auto v = std::roundf(LinearToSRGB(static_cast<float>(i) / 255.f) * 255.f);
      table[i] = static_cast<uint8_t>(v);
    }
    return table;
  }();
  return table;
}
}  // namespace tgfx
