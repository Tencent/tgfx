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

#include "Swizzle.h"

namespace tgfx {
// The normal component swizzles map to key values 0-3. We set the key for constant 1 to the
// next int.
static const int k1KeyValue = 4;

static float ComponentIdxToFloat(const Color& color, int idx) {
  if (idx <= 3) {
    return color[idx];
  }
  if (idx == k1KeyValue) {
    return 1.0f;
  }
  return -1.0f;
}

Color Swizzle::applyTo(const Color& color) const {
  int idx;
  uint32_t k = key;
  // Index of the input color that should be mapped to output r.
  idx = static_cast<int>(k & 15);
  float outR = ComponentIdxToFloat(color, idx);
  k >>= 4;
  idx = static_cast<int>(k & 15);
  float outG = ComponentIdxToFloat(color, idx);
  k >>= 4;
  idx = static_cast<int>(k & 15);
  float outB = ComponentIdxToFloat(color, idx);
  k >>= 4;
  idx = static_cast<int>(k & 15);
  float outA = ComponentIdxToFloat(color, idx);
  return {outR, outG, outB, outA};
}
}  // namespace tgfx