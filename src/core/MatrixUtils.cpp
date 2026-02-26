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

#include "core/MatrixUtils.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

bool MatrixUtils::PreservesAngles(const Matrix& matrix) {
  const auto mask = matrix.getType();
  if (mask <= Matrix::TranslateMask) {
    return true;
  }
  if (mask & Matrix::PerspectiveMask) {
    return false;
  }
  const auto sx = matrix.getScaleX();
  const auto sy = matrix.getScaleY();
  const auto kx = matrix.getSkewX();
  const auto ky = matrix.getSkewY();
  // Check for degenerate matrix.
  if (const auto det = sx * sy - kx * ky; FloatNearlyZero(det)) {
    return false;
  }
  // Orthogonality alone suffices — non-uniform scaling preserves right angles.
  // Do NOT add an equal-length check; it would incorrectly reject valid transforms.
  const auto dot = sx * kx + ky * sy;
  return FloatNearlyZero(dot);
}

}  // namespace tgfx
