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

#include "GLSLUnrolledBinaryGradientColorizer.h"
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

  return allocator->make<GLSLUnrolledBinaryGradientColorizer>(
      intervalCount, scales, biases,
      Rect::MakeLTRB(thresholds[0], thresholds[1], thresholds[2], thresholds[3]),
      Rect::MakeLTRB(thresholds[4], thresholds[5], thresholds[6], 0.0));
}

GLSLUnrolledBinaryGradientColorizer::GLSLUnrolledBinaryGradientColorizer(
    int intervalCount, Color* scales, Color* biases, Rect thresholds1_7, Rect thresholds9_13)
    : UnrolledBinaryGradientColorizer(intervalCount, scales, biases, thresholds1_7,
                                      thresholds9_13) {
}

static void SetColorUniform(UniformData* uniformData, const std::string& name, const Color& value) {
  if (uniformData->hasField(name)) {
    uniformData->setData(name, value);
  }
}

void GLSLUnrolledBinaryGradientColorizer::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  if (fragmentUniformData->hasField("IntervalCount")) {
    fragmentUniformData->setData("IntervalCount", intervalCount);
  }
  SetColorUniform(fragmentUniformData, "scale0_1", scale0_1);
  SetColorUniform(fragmentUniformData, "scale2_3", scale2_3);
  SetColorUniform(fragmentUniformData, "scale4_5", scale4_5);
  SetColorUniform(fragmentUniformData, "scale6_7", scale6_7);
  SetColorUniform(fragmentUniformData, "scale8_9", scale8_9);
  SetColorUniform(fragmentUniformData, "scale10_11", scale10_11);
  SetColorUniform(fragmentUniformData, "scale12_13", scale12_13);
  SetColorUniform(fragmentUniformData, "scale14_15", scale14_15);
  SetColorUniform(fragmentUniformData, "bias0_1", bias0_1);
  SetColorUniform(fragmentUniformData, "bias2_3", bias2_3);
  SetColorUniform(fragmentUniformData, "bias4_5", bias4_5);
  SetColorUniform(fragmentUniformData, "bias6_7", bias6_7);
  SetColorUniform(fragmentUniformData, "bias8_9", bias8_9);
  SetColorUniform(fragmentUniformData, "bias10_11", bias10_11);
  SetColorUniform(fragmentUniformData, "bias12_13", bias12_13);
  SetColorUniform(fragmentUniformData, "bias14_15", bias14_15);
  fragmentUniformData->setData("thresholds1_7", thresholds1_7);
  fragmentUniformData->setData("thresholds9_13", thresholds9_13);
  // Select this colorizer in the unified gradient kernel (absent in other programs).
  fragmentUniformData->setDataOptional("ColorizerKind", 2);
}
}  // namespace tgfx
