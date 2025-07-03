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

#include "FauxBoldScale.h"

namespace tgfx {
static constexpr float StdFakeBoldInterpKeys[] = {
    9.f,
    36.f,
};
static constexpr float StdFakeBoldInterpValues[] = {
    1.f / 24.f,
    1.f / 32.f,
};

inline float Interpolate(const float& a, const float& b, const float& t) {
  return a + (b - a) * t;
}

/**
 * Interpolate along the function described by (keys[length], values[length])
 * for the passed searchKey. SearchKeys outside the range keys[0]-keys[Length]
 * clamp to the min or max value. This function assumes the number of pairs
 * (length) will be small and a linear search is used.
 *
 * Repeated keys are allowed for discontinuous functions (so long as keys is
 * monotonically increasing). If key is the value of a repeated scalar in
 * keys the first one will be used.
 */
static float FloatInterpFunc(float searchKey, const float keys[], const float values[],
                             int length) {
  int right = 0;
  while (right < length && keys[right] < searchKey) {
    ++right;
  }
  // Could use sentinel values to eliminate conditionals, but since the
  // tables are taken as input, a simpler format is better.
  if (right == length) {
    return values[length - 1];
  }
  if (right == 0) {
    return values[0];
  }
  // Otherwise, interpolate between right - 1 and right.
  float leftKey = keys[right - 1];
  float rightKey = keys[right];
  float t = (searchKey - leftKey) / (rightKey - leftKey);
  return Interpolate(values[right - 1], values[right], t);
}

float FauxBoldScale(float textSize) {
  return FloatInterpFunc(textSize, StdFakeBoldInterpKeys, StdFakeBoldInterpValues, 2);
}

}  // namespace tgfx
