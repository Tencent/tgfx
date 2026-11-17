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

/**
 * Coefficients of a smoothed right-angle corner with unit radius.
 *
 * The corner outline consists of an incoming transition curve, a retained arc and an outgoing
 * transition curve. The transition curves are cubic bezier curves, and the retained arc is
 * approximated by a cubic bezier curve. The two transition curves mirror each other across the
 * corner's bisector, so the coefficients only describe the incoming transition curve and the
 * retained arc.
 */
struct UnitSmoothCornerCoeffs {
  // Distance from the corner vertex to the start point of the incoming transition curve, where
  // the curve leaves the edge.
  float start = 1.0f;
  // Spacing between the two inner control points of the incoming transition curve. The start point
  // and both inner control points lie on the edge, with the inner control points 2 and 3 steps
  // from the start point.
  float step = 0.0f;
  // Offsets of the retained arc's start point, which ends the incoming transition curve, from the
  // inner control point nearer the arc: along the direction from the curve's start point toward
  // the corner vertex, and perpendicular to it toward the inside of the shape.
  float arcOffsetAlong = 0.0f;
  float arcOffsetCross = 0.0f;
  // Handle length of the bezier curve that approximates the retained arc.
  float arcHandle = 0.0f;
};

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
   * Conservatively tests whether a point lies inside the RRect.
   * @param rRect  the rounded rectangle
   * @param point  the point to test
   * @return true if the point is guaranteed to lie inside the RRect; false if it lies outside or
   *         containment cannot be decided, which is always the case for smoothed RRects
   */
  static bool ConservativeContainsPoint(const RRect& rRect, const Point& point);

  /**
   * Conservatively tests whether a rect lies entirely inside the RRect.
   * @param rRect  the rounded rectangle
   * @param rect  the rect to test
   * @return true if the rect is guaranteed to lie entirely inside the RRect; false if it does not
   *         or containment cannot be decided
   * @note Because an RRect is convex, this holds exactly when all four corners of the rect are
   *       contained.
   */
  static bool ConservativeContainsRect(const RRect& rRect, const Rect& rect);

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

  /**
   * Tests whether the two corners of either diagonal pair together span more than the side length
   * on the x or y axis.
   * @param rRect the rounded rectangle
   * @return true if either diagonal corner pair overlaps on the x or y axis
   */
  static bool HasDiagonalCornerAxisOverlap(const RRect& rRect);

  /**
   * Computes the coefficients of a smoothed corner with unit radius.
   * @param smoothness the corner smoothness, clamped to [0, 1]
   * @return the coefficients for the clamped smoothness
   */
  static UnitSmoothCornerCoeffs ComputeUnitSmoothCornerCoeffs(float smoothness);
};

}  // namespace tgfx
