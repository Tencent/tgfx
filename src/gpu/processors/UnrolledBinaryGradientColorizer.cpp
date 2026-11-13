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

#include "UnrolledBinaryGradientColorizer.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
static constexpr int MaxIntervals = 8;

PlacementPtr<UnrolledBinaryGradientColorizer> UnrolledBinaryGradientColorizer::Make(
    BlockAllocator* allocator, const Color* colors, const float* positions, int count) {
  // Depending on how the positions resolve into hard stops or regular stops, the number of
  // intervals specified by the number of colors/positions can change. For instance, a plain
  // 3 color gradient is two intervals, but a 4 color gradient with a hard stop is also
  // two intervals. At the most extreme end, an 8 interval gradient made entirely of hard
  // stops has 16 colors.

  if (count > MaxColorCount) {
    // Definitely cannot represent this gradient configuration
    return nullptr;
  }

  // The raster implementation also uses scales and biases, but since they must be calculated
  // after the dst color space is applied, it limits our ability to cache their values.
  Color scales[MaxIntervals];
  Color biases[MaxIntervals];
  float thresholds[MaxIntervals];

  int intervalCount = 0;

  for (int i = 0; i < count - 1; i++) {
    if (intervalCount >= MaxIntervals) {
      // Already reached MaxIntervals, and haven't run out of color stops so this
      // gradient cannot be represented by this shader.
      return nullptr;
    }

    auto t0 = positions[i];
    auto t1 = positions[i + 1];
    auto dt = t1 - t0;
    // If the interval is empty, skip to the next interval. This will automatically create
    // distinct hard stop intervals as needed. It also protects against malformed gradients
    // that have repeated hard stops at the very beginning that are effectively unreachable.
    if (FloatNearlyZero(dt)) {
      continue;
    }

    for (int j = 0; j < 4; ++j) {
      auto c0 = colors[i][j];
      auto c1 = colors[i + 1][j];
      auto scale = (c1 - c0) / dt;
      auto bias = c0 - t0 * scale;
      scales[intervalCount][j] = scale;
      biases[intervalCount][j] = bias;
    }
    thresholds[intervalCount] = t1;
    intervalCount++;
  }

  // set the unused values to something consistent
  for (int i = intervalCount; i < MaxIntervals; i++) {
    scales[i] = Color::Transparent();
    biases[i] = Color::Transparent();
    thresholds[i] = 0.0;
  }

  return allocator->make<UnrolledBinaryGradientColorizer>(
      intervalCount, scales, biases,
      Rect::MakeLTRB(thresholds[0], thresholds[1], thresholds[2], thresholds[3]),
      Rect::MakeLTRB(thresholds[4], thresholds[5], thresholds[6], 0.0));
}

}  // namespace tgfx
