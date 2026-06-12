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

#include <optional>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class RRectUtils {
 public:
  /**
   * Conservative geometric intersection of two RRects sharing the same coordinate space.
   *
   * Returns the intersection RRect when the result is exactly representable as an RRect.
   * Returns std::nullopt when:
   *   - the bounding rects do not intersect (caller should treat as empty),
   *   - any corner of the candidate intersection lies outside one of the inputs,
   *   - per-corner radii from the two inputs cross at the same corner (e.g. one larger in X
   *     and the other larger in Y, with neither dominating),
   *   - the resulting per-corner radii would need to be scaled down to fit the rect (sum of
   *     adjacent radii exceeds the side length).
   *
   * Empty intersection bounds are reported via an RRect of Type::Rect with empty bounds.
   */
  static std::optional<RRect> ConservativeIntersect(const RRect& a, const RRect& b);

  /**
   * Returns the largest axis-aligned rectangle fully contained within the RRect. For a plain
   * rectangle the result equals the rect itself. For rounded rectangles the function evaluates
   * three candidate inscribed rectangles (horizontal inset, vertical inset, and diagonal inset)
   * and returns the one with the largest area.
   */
  static Rect InnerBounds(const RRect& rr);

  /**
   * Attempts to apply an axis-aligned transform to the RRect, reordering per-corner radii to
   * preserve the rounded shape. Succeeds only when the matrix preserves axis alignment
   * (identity, translate, non-zero scale, mirror, and rotations by multiples of 90 degrees).
   * Returns std::nullopt when the matrix has perspective, contains skew or arbitrary rotation,
   * the resulting bounds are non-finite or empty, or the resulting per-corner radii fail the
   * validity check.
   */
  static std::optional<RRect> TryAxisAlignedTransform(const RRect& src, const Matrix& matrix);
};

}  // namespace tgfx
