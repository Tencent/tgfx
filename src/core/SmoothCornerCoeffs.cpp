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

#include "SmoothCornerCoeffs.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

SmoothCornerCoeffs ComputeSmoothCornerCoeffs(float smoothness) {
  const auto s = std::clamp(smoothness, 0.0f, 1.0f);
  const auto turnAngle = M_PI_F * 0.25f * s;
  const auto arcSweep = M_PI_2_F * (1.0f - s);
  const auto tanTurn = tanf(turnAngle);
  SmoothCornerCoeffs coeffs = {};
  coeffs.tangentRatio = 1.0f + s;
  coeffs.arcChordRatio = sinf(arcSweep * 0.5f) * FLOAT_SQRT2;
  coeffs.endOffsetX = tanf(turnAngle * 0.5f) * cosf(turnAngle);
  coeffs.endOffsetY = coeffs.endOffsetX * tanTurn;
  coeffs.controlStep =
      (coeffs.tangentRatio - coeffs.arcChordRatio - (1.0f + tanTurn) * coeffs.endOffsetX) / 3.0f;
  coeffs.arcHandle = (4.0f / 3.0f) * tanf(arcSweep * 0.25f);
  coeffs.turnSin = sinf(turnAngle);
  coeffs.turnCos = cosf(turnAngle);
  return coeffs;
}

}  // namespace tgfx
