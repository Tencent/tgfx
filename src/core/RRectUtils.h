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
   * Computes the conservative geometric intersection of two RRects.
   * @param a  the first rounded rectangle
   * @param b  the second rounded rectangle
   * @return the intersection RRect when it is exactly representable as an RRect; an empty RRect
   *         (Type::Rect with empty bounds) when the bounding rects do not intersect; std::nullopt
   *         when the intersection cannot be represented exactly as a single RRect
   */
  static std::optional<RRect> ConservativeIntersect(const RRect& a, const RRect& b);

  /**
   * Tests whether a point lies inside the RRect.
   * @param rRect  the rounded rectangle
   * @param point  the point to test
   * @return true if the point lies inside the RRect
   */
  static bool ContainsPoint(const RRect& rRect, const Point& point);

  /**
   * Tests whether a rect lies entirely inside the RRect.
   * @param rRect  the rounded rectangle
   * @param rect  the rect to test
   * @return true if the rect lies entirely inside the RRect
   * @note Because an RRect is convex, this holds exactly when all four corners of the rect are
   *       contained.
   */
  static bool ContainsRect(const RRect& rRect, const Rect& rect);

  /**
   * Computes a large axis-aligned rectangle fully contained within the RRect.
   * @param rr  the rounded rectangle
   * @return a large inscribed axis-aligned rect; equals the rect itself for a plain rectangle
   * @note This is a cheap approximation and is not guaranteed to be the maximal inscribed
   *       rectangle.
   */
  static Rect InnerBounds(const RRect& rr);

  /**
   * Attempts to apply an axis-aligned transform to the RRect, adjusting the per-corner radii to
   * match the transform.
   * @param src  the source rounded rectangle
   * @param matrix  the transform to apply
   * @return the transformed RRect when the matrix preserves axis alignment (identity, translate,
   *         non-zero scale, mirror, or multiples of 90-degree rotation); std::nullopt when the
   *         matrix is not axis-aligned or the transform cannot produce a valid RRect.
   */
  static std::optional<RRect> TryAxisAlignedTransform(const RRect& src, const Matrix& matrix);
};

}  // namespace tgfx
