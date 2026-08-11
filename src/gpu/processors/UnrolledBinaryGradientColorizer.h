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

#pragma once

#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/Color.h"

namespace tgfx {
class UnrolledBinaryGradientColorizer : public FragmentProcessor {
 public:
  static constexpr int MaxColorCount = 16;

  static PlacementPtr<UnrolledBinaryGradientColorizer> Make(BlockAllocator* allocator,
                                                            const Color* colors,
                                                            const float* positions, int count);

  std::string name() const override {
    return "UnrolledBinaryGradientColorizer";
  }

  int getIntervalCount() const {
    return intervalCount;
  }

  // Copies the piecewise-linear segments as scale/bias pairs plus the search thresholds.
  void getSegments(Color* outScales, Color* outBiases, Rect* outThresholds1_7,
                   Rect* outThresholds9_13) const {
    outScales[0] = scale0_1;
    outScales[1] = scale2_3;
    outScales[2] = scale4_5;
    outScales[3] = scale6_7;
    outScales[4] = scale8_9;
    outScales[5] = scale10_11;
    outScales[6] = scale12_13;
    outScales[7] = scale14_15;
    outBiases[0] = bias0_1;
    outBiases[1] = bias2_3;
    outBiases[2] = bias4_5;
    outBiases[3] = bias6_7;
    outBiases[4] = bias8_9;
    outBiases[5] = bias10_11;
    outBiases[6] = bias12_13;
    outBiases[7] = bias14_15;
    *outThresholds1_7 = thresholds1_7;
    *outThresholds9_13 = thresholds9_13;
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  UnrolledBinaryGradientColorizer(int intervalCount, Color* scales, Color* biases,
                                  Rect thresholds1_7, Rect thresholds9_13)
      : FragmentProcessor(ClassID()), intervalCount(intervalCount), scale0_1(scales[0]),
        scale2_3(scales[1]), scale4_5(scales[2]), scale6_7(scales[3]), scale8_9(scales[4]),
        scale10_11(scales[5]), scale12_13(scales[6]), scale14_15(scales[7]), bias0_1(biases[0]),
        bias2_3(biases[1]), bias4_5(biases[2]), bias6_7(biases[3]), bias8_9(biases[4]),
        bias10_11(biases[5]), bias12_13(biases[6]), bias14_15(biases[7]),
        thresholds1_7(thresholds1_7), thresholds9_13(thresholds9_13) {
  }

  int intervalCount;
  Color scale0_1;
  Color scale2_3;
  Color scale4_5;
  Color scale6_7;
  Color scale8_9;
  Color scale10_11;
  Color scale12_13;
  Color scale14_15;
  Color bias0_1;
  Color bias2_3;
  Color bias4_5;
  Color bias6_7;
  Color bias8_9;
  Color bias10_11;
  Color bias12_13;
  Color bias14_15;
  Rect thresholds1_7;
  Rect thresholds9_13;
};
}  // namespace tgfx
