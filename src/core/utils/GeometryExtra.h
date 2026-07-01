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

#pragma once

#include <cmath>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * Defines the maximum distance, in device pixels, a draw can extend beyond a clip's boundary and
 * still be considered 'on the other side'. This tolerance absorbs the floating-point rounding noise
 * accumulated while computing analytic bounds, so a value like 100.0003 is not treated as crossing
 * into the next pixel.
 *
 * The value 1e-3 is justified visually for the anti-aliased case. There coverage equals the
 * fraction of a unit-sized pixel that lies inside the edge, so a positional shift of d pixels moves
 * an axis-aligned edge by at most d in coverage as well; the sensitivity of coverage to position is
 * bounded by 1 per pixel, which lets the pixel-space tolerance be compared directly against a
 * coverage threshold. Keeping the shift under 0.5 * 1/256 (half a step of 8-bit quantization,
 * ~1.95e-3) leaves the quantized pixel value unchanged, and 1e-3 sits safely below that bound. This
 * is an order-of-magnitude safety argument, not an exact identity, and it only covers the AA edge;
 * non-AA edges are hard and rely on a separate rounding tolerance instead.
 */
static constexpr float BOUNDS_TOLERANCE = 1e-3f;

inline bool IsPixelAligned(float value) {
  return fabsf(roundf(value) - value) <= BOUNDS_TOLERANCE;
}

inline bool IsPixelAligned(const Rect& rect) {
  return IsPixelAligned(rect.left) && IsPixelAligned(rect.top) && IsPixelAligned(rect.right) &&
         IsPixelAligned(rect.bottom);
}

inline Rect ToLocalBounds(const Rect& bounds, const Matrix& viewMatrix) {
  Matrix invertMatrix = {};
  if (!viewMatrix.invert(&invertMatrix)) {
    return {};
  }
  auto localBounds = bounds;
  invertMatrix.mapRect(&localBounds);
  return localBounds;
}

}  // namespace tgfx
