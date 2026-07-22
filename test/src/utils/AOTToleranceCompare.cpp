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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "utils/AOTToleranceCompare.h"
#include <algorithm>
#include <cmath>

namespace tgfx {

static int ToByte(float channel) {
  auto scaled = static_cast<int>(std::lround(channel * 255.0f));
  return std::min(255, std::max(0, scaled));
}

AOTToleranceResult AOTToleranceCompare::Compare(const Pixmap& reference, const Pixmap& candidate,
                                                const AOTToleranceSpec& spec) {
  AOTToleranceResult result;
  if (reference.width() != candidate.width() || reference.height() != candidate.height()) {
    result.sizeMismatch = true;
    result.structuralDifference = true;
    result.passed = false;
    return result;
  }
  result.width = reference.width();
  result.height = candidate.height();
  result.totalPixelCount =
      static_cast<uint64_t>(result.width) * static_cast<uint64_t>(result.height);
  for (int y = 0; y < result.height; ++y) {
    for (int x = 0; x < result.width; ++x) {
      auto refColor = reference.getColor(x, y);
      auto candColor = candidate.getColor(x, y);
      int dr = std::abs(ToByte(refColor.red) - ToByte(candColor.red));
      int dg = std::abs(ToByte(refColor.green) - ToByte(candColor.green));
      int db = std::abs(ToByte(refColor.blue) - ToByte(candColor.blue));
      int da = std::abs(ToByte(refColor.alpha) - ToByte(candColor.alpha));
      int pixelMax = std::max(std::max(dr, dg), std::max(db, da));
      if (pixelMax > 0) {
        result.diffPixelCount++;
      }
      if (pixelMax > result.maxChannelDiff) {
        result.maxChannelDiff = pixelMax;
      }
      if (pixelMax >= spec.structuralChannelDiff) {
        result.structuralDifference = true;
      }
    }
  }
  result.passed = !result.structuralDifference && result.maxChannelDiff <= spec.maxChannelDiff &&
                  result.diffPixelRatio() <= spec.maxDiffPixelRatio;
  return result;
}

}  // namespace tgfx
