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

#pragma once

#include <cmath>

namespace tgfx {

/**
 * Defines the maximum distance a draw can extend beyond a clip's boundary and still be considered
 * 'on the other side'. This tolerance accounts for potential floating point rounding errors. The
 * value of 1e-3 is chosen because, in the coverage case, as long as coverage stays within
 * 0.5 * 1/256 of its intended value, it shouldn't affect the final pixel values.
 */
static constexpr float BOUNDS_TOLERANCE = 1e-3f;

inline bool IsPixelAligned(float value) {
  return fabsf(roundf(value) - value) <= BOUNDS_TOLERANCE;
}

}  // namespace tgfx
