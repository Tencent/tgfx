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

#include "GLSLShapeBlurFunctions.h"
#include <algorithm>

namespace tgfx {

// Quadrature point counts the rounded-rect coverage form uses along the axis it does not solve
// in closed form. At a fixed count the quadrature error grows with the ratio between the corner's
// two semi-axes, so the count is picked per shape rather than fixed.
static constexpr int LOW_QUADRATURE_COUNT = 4;
static constexpr int HIGH_QUADRATURE_COUNT = 8;

// The corner semi-axis ratio above which the high count is used instead of the low one.
static constexpr float SEMI_AXIS_RATIO_THRESHOLD = 2.0f;

int GetRRectBlurQuadratureCount(const Point& cornerOverSigma) {
  // A zero corner (no arc) makes every slice identical along the summed axis, so the
  // quadrature's sampling error is a common factor of the sum and the weight sum, which cancels
  // in the division — any count is exact, and the low count suffices.
  if (cornerOverSigma.x <= 0.0f || cornerOverSigma.y <= 0.0f) {
    return LOW_QUADRATURE_COUNT;
  }
  // Worst errors measured over 337 sigma-normalized shapes (corner semi-axes from 0.25 to 10,
  // six half-size pairs), in 8-bit levels: 16.01 with the low count everywhere, 7.90 with the
  // high count everywhere. With this dispatch, shapes below the threshold reach only 6.16 and
  // the overall worst stays 7.90.
  //
  // The threshold stays at 2: at 2.5 the low-count worst would rise from 6.16 to 7.57.
  const auto ratio =
      std::max(cornerOverSigma.x / cornerOverSigma.y, cornerOverSigma.y / cornerOverSigma.x);
  return ratio > SEMI_AXIS_RATIO_THRESHOLD ? HIGH_QUADRATURE_COUNT : LOW_QUADRATURE_COUNT;
}

}  // namespace tgfx
