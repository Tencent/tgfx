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
#include "tgfx/core/Stroke.h"

namespace tgfx {

/**
 * Coefficients of a smoothed right-angle corner with unit radius.
 *
 * The corner contour C consists of the incoming transition curve C1, the retained arc C2 and the
 * outgoing transition curve C3. C1 and C3 are cubic Bezier curves, and C2 is approximated by a
 * cubic Bezier curve. B0 to B3 denote the control points of a curve along the direction of travel.
 * PS is the start of C2, where C1 ends, and PE is the end of C2, where C3 starts. C1 and C3 mirror
 * each other across the corner's bisector, so the coefficients only describe C1 and C2.
 */
struct UnitSmoothCornerCoeffs {
  // Distance from the corner vertex to B0 of C1, where the curve leaves the edge.
  float start = 1.0f;
  // Spacing between B1 and B2 of C1. B0, B1 and B2 lie on the edge, with B1 and B2 2 and 3 steps
  // from B0.
  float step = 0.0f;
  // Offsets of PS from B2 of C1: along the direction from B0 toward the corner vertex, and
  // perpendicular to it toward the inside of the shape.
  float arcOffsetAlong = 0.0f;
  float arcOffsetCross = 0.0f;
  // Handle length of the cubic Bezier curve that approximates C2.
  float arcHandle = 0.0f;
};

/**
 * Describes how a rounded rectangle is drawn under a given matrix.
 */
enum class RRectDrawType {
  // The maximum scale of the matrix is nearly zero, so nothing is drawn.
  None,
  // Every corner has a radius below half a device pixel, so the bounding rect is drawn instead.
  Rect,
  // The rounded rectangle is drawn as a path.
  Path,
  // The rounded rectangle is drawn by the dedicated rounded rectangle ops.
  RRect
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

  /**
   * Tests whether the rounded rectangle must be drawn as a path because the dedicated rounded
   * rectangle ops cannot draw it accurately under the given matrix and stroke.
   * @param rRect the rounded rectangle
   * @param stroke the stroke, or nullptr for a fill
   * @param viewMatrix the matrix that maps the rounded rectangle to device space
   * @param antiAlias whether the rounded rectangle is antialiased
   * @return true if the rounded rectangle must be drawn as a path
   */
  static bool UseDrawPath(const RRect& rRect, const Stroke* stroke, const Matrix& viewMatrix,
                          bool antiAlias);

  /**
   * Chooses how to draw the rounded rectangle under the given matrix.
   * @param rRect the rounded rectangle, with a non-empty bounding rect
   * @param stroke the stroke, or nullptr for a fill
   * @param viewMatrix the matrix that maps the rounded rectangle to device space
   * @param antiAlias whether the rounded rectangle is antialiased
   * @return the way to draw the rounded rectangle
   */
  static RRectDrawType GetDrawType(const RRect& rRect, const Stroke* stroke,
                                   const Matrix& viewMatrix, bool antiAlias);
};

}  // namespace tgfx
