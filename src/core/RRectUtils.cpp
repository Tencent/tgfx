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

#include "RRectUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

// Enum order must match RRect::radii() indexing, since values are cast to array indices.
enum class RectCorner { TL = 0, TR, BR, BL };  // TopLeft, TopRight, BottomRight, BottomLeft

static inline Point GetCorner(const Rect& r, RectCorner corner) {
  switch (corner) {
    case RectCorner::TL:
      return {r.left, r.top};
    case RectCorner::TR:
      return {r.right, r.top};
    case RectCorner::BR:
      return {r.right, r.bottom};
    case RectCorner::BL:
      return {r.left, r.bottom};
  }
  return {};
}

// Returns true when point is at least as far toward the rect interior as reference, along the given
// corner's inward direction.
static inline bool IsInnerAtCorner(RectCorner corner, const Point& point, const Point& reference) {
  switch (corner) {
    case RectCorner::TL:
      return point.x >= reference.x && point.y >= reference.y;
    case RectCorner::TR:
      return point.x <= reference.x && point.y >= reference.y;
    case RectCorner::BR:
      return point.x <= reference.x && point.y <= reference.y;
    case RectCorner::BL:
      return point.x >= reference.x && point.y <= reference.y;
  }
  return false;
}

// Tests whether the point lies on the inner side of every rounded corner. A point outside all
// corner regions passes; a point inside a corner region must fall within that corner's ellipse.
static bool IsInnerToCorners(const RRect& rr, const Point& point) {
  const auto& rect = rr.rect();
  const auto& radii = rr.radii();
  Point centerOffset = {};
  auto cornerIndex = RectCorner::TL;

  if (rr.type() == RRect::Type::Oval) {
    // An oval is a single centered ellipse; every corner radius equals its half-axes, so use TL and
    // measure the offset from the ellipse center.
    centerOffset = {point.x - rect.centerX(), point.y - rect.centerY()};
    cornerIndex = RectCorner::TL;
  } else {
    const auto tl = static_cast<size_t>(RectCorner::TL);
    const auto tr = static_cast<size_t>(RectCorner::TR);
    const auto br = static_cast<size_t>(RectCorner::BR);
    const auto bl = static_cast<size_t>(RectCorner::BL);
    if (point.x < rect.left + radii[tl].x && point.y < rect.top + radii[tl].y) {
      cornerIndex = RectCorner::TL;
      centerOffset = {point.x - (rect.left + radii[tl].x), point.y - (rect.top + radii[tl].y)};
    } else if (point.x < rect.left + radii[bl].x && point.y > rect.bottom - radii[bl].y) {
      cornerIndex = RectCorner::BL;
      centerOffset = {point.x - (rect.left + radii[bl].x), point.y - (rect.bottom - radii[bl].y)};
    } else if (point.x > rect.right - radii[tr].x && point.y < rect.top + radii[tr].y) {
      cornerIndex = RectCorner::TR;
      centerOffset = {point.x - (rect.right - radii[tr].x), point.y - (rect.top + radii[tr].y)};
    } else if (point.x > rect.right - radii[br].x && point.y > rect.bottom - radii[br].y) {
      cornerIndex = RectCorner::BR;
      centerOffset = {point.x - (rect.right - radii[br].x), point.y - (rect.bottom - radii[br].y)};
    } else {
      // Not within any corner region, so the point is on the inner side of all corners.
      return true;
    }
  }

  // Ellipse test (rx, ry are the corner's half-axes): (x/rx)^2 + (y/ry)^2 <= 1, simplified to
  // x^2 * ry^2 + y^2 * rx^2 <= (rx * ry)^2.
  const float rx = radii[static_cast<size_t>(cornerIndex)].x;
  const float ry = radii[static_cast<size_t>(cornerIndex)].y;
  const float ellipseTest =
      centerOffset.x * centerOffset.x * ry * ry + centerOffset.y * centerOffset.y * rx * rx;
  const float limit = (rx * ry) * (rx * ry);
  return ellipseTest <= limit;
}

// Computes the intersection corner radii at the given corner of intersectionRect. Returns
// std::nullopt when the intersection cannot be exactly represented as an RRect at that corner.
static std::optional<Point> GetIntersectionRadii(const RRect& a, const RRect& b,
                                                 const Rect& intersectionRect, RectCorner corner) {
  const Point interCorner = GetCorner(intersectionRect, corner);
  const Point aCorner = GetCorner(a.rect(), corner);
  const Point bCorner = GetCorner(b.rect(), corner);

  if (interCorner == aCorner && interCorner == bCorner) {
    // Both rrects share this corner anchor. Pick the dominant one.
    const Point aRadii = a.radii()[static_cast<size_t>(corner)];
    const Point bRadii = b.radii()[static_cast<size_t>(corner)];
    if (aRadii.x >= bRadii.x && aRadii.y >= bRadii.y) {
      return aRadii;
    }
    if (bRadii.x >= aRadii.x && bRadii.y >= aRadii.y) {
      return bRadii;
    }
    // One radius is larger in x and the other in y, so the two corner arcs cross and the
    // intersection at this corner cannot be represented as a single ellipse.
    return std::nullopt;
  }

  // The remaining cases handle when the intersection corner coincides with one input's corner. That
  // input supplies the radius; it is valid only if its corner stays inside the other input at this
  // corner. When the two radii are equal, this reduces to comparing the two corner anchors
  // (IsInnerAtCorner); otherwise the corner must lie within the other's rounded outline
  // (IsInnerToCorners). If neither holds, the corner cannot be represented and nullopt is returned.
  if (interCorner == aCorner) {
    const Point aRadii = a.radii()[static_cast<size_t>(corner)];
    const Point bRadii = b.radii()[static_cast<size_t>(corner)];
    const bool contained =
        aRadii == bRadii ? IsInnerAtCorner(corner, aCorner, bCorner) : IsInnerToCorners(b, aCorner);
    return contained ? std::optional<Point>(aRadii) : std::nullopt;
  }
  if (interCorner == bCorner) {
    const Point aRadii = a.radii()[static_cast<size_t>(corner)];
    const Point bRadii = b.radii()[static_cast<size_t>(corner)];
    const bool contained =
        aRadii == bRadii ? IsInnerAtCorner(corner, bCorner, aCorner) : IsInnerToCorners(a, bCorner);
    return contained ? std::optional<Point>(bRadii) : std::nullopt;
  }

  // The corner is formed by two straight edges of a and b, so it is a sharp corner ({0, 0} radii)
  // that must lie within both inputs' rounded outlines.
  if (IsInnerToCorners(a, interCorner) && IsInnerToCorners(b, interCorner)) {
    return Point{0, 0};
  }
  return std::nullopt;
}

// Returns true if any adjacent pair of radii along a side exceeds that side's length.
static bool RadiiOverflow(const Rect& rect, const std::array<Point, 4>& radii) {
  const float w = rect.width();
  const float h = rect.height();
  // Radii order is TL, TR, BR, BL; check each side's two adjacent corners.
  return (radii[0].x + radii[1].x > w) || (radii[3].x + radii[2].x > w) ||
         (radii[0].y + radii[3].y > h) || (radii[1].y + radii[2].y > h);
}

std::optional<RRect> RRectUtils::ConservativeIntersect(const RRect& a, const RRect& b) {
  if (a == b) {
    return a;
  }
  Rect intersection = a.rect();
  if (!intersection.intersect(b.rect())) {
    // Bounding rects do not overlap; return an explicit empty RRect.
    return RRect();
  }
  // A smoothed corner can cross a general corner of a larger radius, and the intersection then
  // cannot be represented as an RRect.
  if (a.smoothness() > 0.0f || b.smoothness() > 0.0f) {
    return std::nullopt;
  }

  std::array<Point, 4> radii = {};
  for (auto corner : {RectCorner::TL, RectCorner::TR, RectCorner::BR, RectCorner::BL}) {
    const auto radius = GetIntersectionRadii(a, b, intersection, corner);
    if (!radius) {
      return std::nullopt;
    }
    radii[static_cast<size_t>(corner)] = *radius;
  }

  // If adjacent radii on any side exceed the side length, the intersection cannot be represented
  // exactly as an RRect.
  if (RadiiOverflow(intersection, radii)) {
    return std::nullopt;
  }

  // All radii from valid RRects are non-negative and finite, so setRectRadii will not change them.
  RRect result = {};
  result.setRectRadii(intersection, radii);
  return result;
}

bool RRectUtils::ConservativeContainsPoint(const RRect& rRect, const Point& point) {
  // IsInnerToCorners tests against the ellipse equation of each corner. Smoothed corners would need
  // a costlier test against their cubic curves C1 and C3, so containment is reported as undecided.
  if (rRect.smoothness() > 0.0f) {
    return false;
  }
  return rRect.rect().contains(point.x, point.y) && IsInnerToCorners(rRect, point);
}

bool RRectUtils::ConservativeContainsRect(const RRect& rRect, const Rect& rect) {
  return ConservativeContainsPoint(rRect, {rect.left, rect.top}) &&
         ConservativeContainsPoint(rRect, {rect.right, rect.top}) &&
         ConservativeContainsPoint(rRect, {rect.right, rect.bottom}) &&
         ConservativeContainsPoint(rRect, {rect.left, rect.bottom});
}

Rect RRectUtils::InnerBounds(const RRect& rr) {
  if (rr.rect().isEmpty() || rr.type() == RRect::Type::Rect) {
    return rr.rect();
  }

  auto innerBounds = rr.rect();
  const auto& r = rr.radii();
  const auto leftShift = std::max(r[0].x, r[3].x);
  const auto topShift = std::max(r[0].y, r[1].y);
  const auto rightShift = std::max(r[1].x, r[2].x);
  const auto bottomShift = std::max(r[3].y, r[2].y);
  const auto dw = leftShift + rightShift;
  const auto dh = topShift + bottomShift;
  // Smoothed corners occupy (1 + smoothness) * radius along each edge.
  const auto rScale = 1.0f + rr.smoothness();
  const auto horizArea = (innerBounds.width() - rScale * dw) * innerBounds.height();
  const auto vertArea = (innerBounds.height() - rScale * dh) * innerBounds.width();
  // The maximum inscribed rectangle at the corner ellipse has a corner at
  // sqrt(2)/2 * (rX, rY). Scale all corner shifts by (1 - sqrt(2)/2) to get the safe inset.
  // Add a small epsilon so the shifted corners stay safely inside the curves.
  // A smoothed corner passes through the same point, as the middle of C2 or the join of C1 and C3,
  // so smoothness does not change this inset.
  static constexpr float diagInsetScale = (1.0f - 0.70710678118f) + 1e-5f;
  const auto innerArea =
      (innerBounds.width() - diagInsetScale * dw) * (innerBounds.height() - diagInsetScale * dh);

  if (horizArea > vertArea && horizArea > innerArea) {
    innerBounds.left += rScale * leftShift;
    innerBounds.right -= rScale * rightShift;
  } else if (vertArea > innerArea) {
    innerBounds.top += rScale * topShift;
    innerBounds.bottom -= rScale * bottomShift;
  } else if (innerArea > 0.f) {
    innerBounds.left += diagInsetScale * leftShift;
    innerBounds.right -= diagInsetScale * rightShift;
    innerBounds.top += diagInsetScale * topShift;
    innerBounds.bottom -= diagInsetScale * bottomShift;
  } else {
    // Unreachable for a valid RRect: ScaleRadii keeps each side's adjacent radii within the side
    // length, so the diagonal inset area stays positive. Guard against a broken invariant.
    DEBUG_ASSERT(false);
    return Rect::MakeEmpty();
  }

  return innerBounds;
}

std::optional<RRect> RRectUtils::TryAxisAlignedTransform(const RRect& src, const Matrix& matrix) {
  if (matrix.isIdentity()) {
    return src;
  }
  if (!matrix.rectStaysRect()) {
    return std::nullopt;
  }

  Rect newRect = matrix.mapRect(src.rect());
  // Under extreme transforms the mapped rect may overflow to non-finite values or collapse to an
  // empty rect (a dimension lost to precision). Neither gives a meaningful transformed RRect, so
  // return nullopt instead.
  if (!std::isfinite(newRect.left) || !std::isfinite(newRect.top) ||
      !std::isfinite(newRect.right) || !std::isfinite(newRect.bottom) || newRect.isEmpty()) {
    return std::nullopt;
  }

  if (src.type() == RRect::Type::Rect) {
    RRect dst = {};
    dst.setRect(newRect);
    return dst;
  }
  if (src.type() == RRect::Type::Oval) {
    RRect dst = {};
    dst.setOval(newRect);
    return dst;
  }

  // Corner index convention matches RRect::radii() layout: 0=TL, 1=TR, 2=BR, 3=BL.
  std::array<Point, 4> radii = src.radii();
  float xScale = matrix.getScaleX();
  float yScale = matrix.getScaleY();
  // A 90 / 270 degree rotation moves each corner to a new position, so remap the radii accordingly.
  if (!matrix.isScaleTranslate()) {
    const bool isClockwise = matrix.getSkewX() < 0;
    // Correct the sign of the scale factors extracted from the skew entries so a pure rotation
    // yields positive factors.
    xScale = matrix.getSkewX() * (isClockwise ? -1.0f : 1.0f);
    yScale = matrix.getSkewY() * (isClockwise ? 1.0f : -1.0f);

    const int dir = isClockwise ? 3 : 1;
    std::array<Point, 4> rotated = {};
    for (size_t i = 0; i < 4; ++i) {
      const auto srcIndex = static_cast<size_t>(static_cast<int>(i) + dir) % 4;
      // Swap X and Y axis for the radii.
      rotated[i].x = src.radii()[srcIndex].y;
      rotated[i].y = src.radii()[srcIndex].x;
    }
    radii = rotated;
  }

  // A negative scale factor means a flip along that axis (a 180 degree rotation flips both axes, a
  // mirror flips one). A flip swaps corners across that axis, so remap the radii below and take the
  // factor back to positive for the scaling.
  const bool flipX = xScale < 0;
  if (flipX) {
    xScale = -xScale;
  }
  const bool flipY = yScale < 0;
  if (flipY) {
    yScale = -yScale;
  }
  for (auto& r : radii) {
    r.x *= xScale;
    r.y *= yScale;
  }
  if (flipX) {
    if (flipY) {
      // Swap with opposite corners.
      std::swap(radii[0], radii[2]);
      std::swap(radii[1], radii[3]);
    } else {
      // Only swap in x.
      std::swap(radii[0], radii[1]);
      std::swap(radii[2], radii[3]);
    }
  } else if (flipY) {
    // Only swap in y.
    std::swap(radii[0], radii[3]);
    std::swap(radii[1], radii[2]);
  }

  RRect dst = {};
  dst.setRectRadii(newRect, radii, src.smoothness());
  return dst;
}

bool RRectUtils::HasDiagonalCornerAxisOverlap(const RRect& rRect) {
  // Other types have equal radii at all corners, so each diagonal sum equals an adjacent sum,
  // which RRect already keeps within the side length.
  if (!rRect.isComplex()) {
    return false;
  }
  const auto rScale = 1.0f + rRect.smoothness();
  const auto& r = rRect.radii();
  const auto w = rRect.rect().width();
  const auto h = rRect.rect().height();
  return (r[0].x + r[2].x) * rScale > w || (r[0].y + r[2].y) * rScale > h ||
         (r[1].x + r[3].x) * rScale > w || (r[1].y + r[3].y) * rScale > h;
}

UnitSmoothCornerCoeffs RRectUtils::ComputeUnitSmoothCornerCoeffs(float smoothness) {
  const auto s = std::clamp(smoothness, 0.0f, 1.0f);
  // Angle the tangent turns through along C1.
  const auto turnAngle = M_PI_F * 0.25f * s;
  // Sweep angle of C2.
  const auto arcSweep = M_PI_2_F * (1.0f - s);
  const auto tanTurn = tanf(turnAngle);
  UnitSmoothCornerCoeffs coeffs = {};
  coeffs.start = 1.0f + s;
  coeffs.arcOffsetAlong = tanf(turnAngle * 0.5f) * cosf(turnAngle);
  coeffs.step = (s + sinf(turnAngle) - coeffs.arcOffsetAlong) / 3.0f;
  coeffs.arcOffsetCross = coeffs.arcOffsetAlong * tanTurn;
  coeffs.arcHandle = (4.0f / 3.0f) * tanf(arcSweep * 0.25f);
  return coeffs;
}

// The sub-half-pixel threshold classifies a corner as "sharp" only when the arc would be
// shorter than one device pixel; therefore the local radius must be projected to device space
// via the current CTM's max scale before comparison. Without this, a tiny local-space radius
// inside a large scale-up (e.g. a 0.9-unit oval under 100x) is wrongly treated as sharp and
// the dedicated RRect op is bypassed.
static bool HasSharpCorner(const RRect& rRect, const Matrix& viewMatrix) {
  auto deviceScale = viewMatrix.getMaxScale();
  if (FloatNearlyZero(deviceScale)) {
    return true;
  }
  for (const auto& r : rRect.radii()) {
    if (r.x * deviceScale < 0.5f || r.y * deviceScale < 0.5f) {
      return true;
    }
  }
  return false;
}

static bool UseDrawPathForSmoothRRect(const Rect& rect, const std::array<Point, 4>& radii,
                                      float smoothness, bool isComplex, const Stroke* stroke,
                                      const Matrix& viewMatrix, bool antiAlias) {
  const auto scales = viewMatrix.getAxisScales();
  // Axis scales that map the local geometry into the space where the shader evaluates distances.
  auto evalScales = scales;
  // Farthest distance from the contour, measured in the space where the shader evaluates distances.
  // It includes the half stroke width and the AA band. A NonAA fill only tests which side of the
  // contour a fragment lies on, so its maxDist is 0.
  auto maxDist = 0.0f;
  if (stroke != nullptr) {
    // Under non-uniform scaling, a stroke's width differs between the two axes in the scaled
    // space, so its distances are evaluated in the unscaled local space where the width is
    // uniform. A hairline (width 0) has a fixed device-space width that is uniform in the scaled
    // space, so it is evaluated there like other strokes.
    const auto unscaledGeometry = stroke->width > 0.0f && scales.x != scales.y;
    evalScales = unscaledGeometry ? Point{1.0f, 1.0f} : scales;
    if (unscaledGeometry) {
      const auto minScale = viewMatrix.getMinScale();
      // A zero minimum scale collapses the RRect into a line, which is drawn as a path.
      if (minScale <= 0.0f) {
        return true;
      }
      maxDist = stroke->width * 0.5f + 0.5f / minScale;
    } else {
      const auto halfStroke = stroke->width > 0.0f ? stroke->width * scales.x * 0.5f : 0.5f;
      maxDist = halfStroke + 0.5f;
    }
    // The distance solver's error grows with the distance from the contour; cap maxDist to
    // keep the measured error subpixel.
    if (maxDist > 50.0f) {
      return true;
    }
  } else if (antiAlias) {
    maxDist = 0.5f;
  }

  // Non-Complex types have identical radii across all four corners, so only the first needs
  // check.
  const auto cornerCount = isComplex ? radii.size() : size_t{1};
  for (size_t i = 0; i < cornerCount; ++i) {
    const auto& r = radii[i];
    // The shader can only produce a square outer join for zero-radius corners. Other join
    // shapes require drawPath.
    if ((r.x <= 0.0f || r.y <= 0.0f) && stroke != nullptr &&
        (stroke->join != LineJoin::Miter || 1.0f / stroke->miterLimit > 1.0f / FLOAT_SQRT2)) {
      return true;
    }
    const auto xRadius = r.x * evalScales.x;
    const auto yRadius = r.y * evalScales.y;
    if (stroke != nullptr && xRadius > 0.0f && yRadius > 0.0f) {
      // A corner region only evaluates its own contour. If an opposite edge is within maxDist of
      // the region, the stroke along that edge is missed.
      if (rect.width() * evalScales.x - (1.0f + smoothness) * xRadius < maxDist ||
          rect.height() * evalScales.y - (1.0f + smoothness) * yRadius < maxDist) {
        return true;
      }
    }
    const auto minRadius = std::min(xRadius, yRadius);
    const auto maxRadius = std::max(xRadius, yRadius);
    // AA fills and all strokes use the fixed-iteration cubic distance solver. The corner's
    // axis ratio affects its accuracy; the measured limit keeps the error subpixel.
    if ((stroke != nullptr || antiAlias) && maxRadius > 4.0f * minRadius) {
      return true;
    }
    // The shader finds the nearest contour segment correctly only within the safe distance
    // R = minRadius^2 / (2 * maxRadius), so maxDist must not exceed it.
    if (maxDist * maxRadius > 0.5f * minRadius * minRadius) {
      return true;
    }
  }
  return false;
}

/**
 * The dedicated RRect op draws stroked rounded corners by offsetting the ellipse equation
 * outward/inward, which assumes a concentric ellipse approximates the true stroke offset
 * curve. The approximation breaks down when the stroke is too thick relative to the corner
 * radii, or when the ellipse is too elongated (major/minor ratio too large). In those cases
 * this function returns true so the caller falls back to drawPath for an accurate stroke.
 */
static bool UseDrawPathForRRect(const std::array<Point, 4>& radii, bool isComplex,
                                const Stroke* stroke, const Matrix& viewMatrix,
                                bool hasSharpCorner) {
  // Sharp corners (sub-half-unit local radius on either axis) make the ellipse implicit
  // equation degenerate: at zero radius the equation is undefined, and under stroke the
  // inner ellipse radius would become negative. Fall back to drawPath so the sharp
  // geometry renders correctly.
  if (hasSharpCorner) {
    return true;
  }
  if (stroke == nullptr) {
    return false;
  }

  const auto scales = viewMatrix.getAxisScales();
  // Per-axis scaled stroke width: local width multiplied by the x/y axis scale factors
  // extracted from the view matrix.
  const Point scaledStroke = {scales.x * stroke->width, scales.y * stroke->width};
  Point halfScaledStroke = {};
  if (FloatNearlyZero(scaledStroke.length())) {
    // Hairline stroke (width == 0) falls back to half a pixel on each axis.
    halfScaledStroke = {0.5f, 0.5f};
  } else {
    halfScaledStroke = scaledStroke * 0.5f;
  }
  const auto halfScaledStrokeLen = halfScaledStroke.length();

  // Non-Complex types have identical radii across all four corners (zero for Rect, equal
  // per-corner for Simple/Oval), so only the first needs check.
  const auto cornerCount = isComplex ? radii.size() : size_t{1};
  for (size_t i = 0; i < cornerCount; ++i) {
    const auto& r = radii[i];
    const auto xRadius = scales.x * r.x;
    const auto yRadius = scales.y * r.y;
    // Half stroke width exceeds corner radius: the inner offset ellipse collapses
    // (inner radius would be non-positive), so the concentric-ellipse approximation
    // cannot be constructed.
    if (halfScaledStroke.x > xRadius || halfScaledStroke.y > yRadius) {
      return true;
    }
    // Thick stroke on an elongated ellipse (major/minor axis ratio > 2): the concentric-ellipse
    // approximation visibly diverges from the true offset curve.
    if (halfScaledStrokeLen > 0.5f && (0.5f * xRadius > yRadius || 0.5f * yRadius > xRadius)) {
      return true;
    }
    // Fall back to drawPath when the stroke half width exceeds the ellipse's curvature radius
    // at an axis endpoint (yRadius^2/xRadius on the x-axis, xRadius^2/yRadius on the y-axis).
    // Past that radius, near the x-axis endpoint there is a range of angles over which the y
    // of the stroke's inner edge decreases as the angle increases, which never happens on a
    // concentric ellipse (likewise for x at the y-axis endpoint).
    // With (hx, hy) = halfScaledStroke, the x-axis check first scales y by hx/hy to make the
    // stroke uniform, so hx > (yRadius*hx/hy)^2/xRadius rearranges to the first check below;
    // the second check swaps the axes.
    if (halfScaledStroke.x * yRadius * yRadius <
        halfScaledStroke.y * halfScaledStroke.y * xRadius) {
      return true;
    }
    if (halfScaledStroke.y * xRadius * xRadius <
        halfScaledStroke.x * halfScaledStroke.x * yRadius) {
      return true;
    }
  }
  return false;
}

bool RRectUtils::UseDrawPath(const RRect& rRect, const Stroke* stroke, const Matrix& viewMatrix,
                             bool antiAlias) {
  // Under perspective, a fixed-width AA ramp in device space maps back to a ramp of varying width
  // in local space, which the rrect shaders cannot express when evaluating the contour locally.
  // NonAA could be supported by interpolating the local coordinates with perspective correction,
  // but the use case is rare, so perspective falls back for now.
  // A skew is accepted as a known visual flaw: it changes the width of the AA ramp measured along
  // the normals of the transformed edges.
  if (viewMatrix.hasPerspective()) {
    return true;
  }
  // For general rrects:
  // The AA path partitions the RRect into a 4x4 grid of quads; a diagonal corner pair
  // overlapping on either axis makes those quads self-intersect. The NonAA path is free of
  // that mesh constraint but its shader still miscomputes coverage when diagonal corner
  // boxes truly overlap. We cannot tell here which path the op will take, so apply the
  // stricter AA rule and fall back to drawPath whenever any axis is crossed.
  // For smooth rrects:
  // The shader does not handle fragments shared by two corner regions.
  if (RRectUtils::HasDiagonalCornerAxisOverlap(rRect)) {
    return true;
  }
  if (rRect.smoothness() > 0.0f) {
    return UseDrawPathForSmoothRRect(rRect.rect(), rRect.radii(), rRect.smoothness(),
                                     rRect.isComplex(), stroke, viewMatrix, antiAlias);
  }
  return UseDrawPathForRRect(rRect.radii(), rRect.isComplex(), stroke, viewMatrix,
                             HasSharpCorner(rRect, viewMatrix));
}

RRectDrawType RRectUtils::GetDrawType(const RRect& rRect, const Stroke* stroke,
                                      const Matrix& viewMatrix, bool antiAlias) {
  // The sub-half-pixel threshold is evaluated in device space, so the local radii are scaled by the
  // current CTM's max scale before comparing. Otherwise a layer-local small RRect inside a
  // scaled-up parent (e.g. a 0.9x0.9 oval inside a 100x scale) would be wrongly degenerated into a
  // rect even though its arcs are clearly visible after the transform. A singular CTM
  // (max scale == 0) projects all geometry to a zero-area region, so nothing visible would be
  // drawn anyway.
  auto deviceScale = viewMatrix.getMaxScale();
  if (FloatNearlyZero(deviceScale)) {
    return RRectDrawType::None;
  }
  auto allSmall = true;
  for (const auto& r : rRect.radii()) {
    if (r.x * deviceScale >= 0.5f && r.y * deviceScale >= 0.5f) {
      allSmall = false;
      break;
    }
  }
  if (allSmall) {
    return RRectDrawType::Rect;
  }
  if (UseDrawPath(rRect, stroke, viewMatrix, antiAlias)) {
    return RRectDrawType::Path;
  }
  return RRectDrawType::RRect;
}

}  // namespace tgfx
