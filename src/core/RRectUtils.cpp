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
#include <array>
#include <cmath>
#include "core/utils/Log.h"

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
  Rect intersection = a.rect();
  if (!intersection.intersect(b.rect())) {
    // Bounding rects do not overlap; return an explicit empty RRect.
    return RRect();
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

bool RRectUtils::ContainsPoint(const RRect& rRect, const Point& point) {
  return rRect.rect().contains(point.x, point.y) && IsInnerToCorners(rRect, point);
}

bool RRectUtils::ContainsRect(const RRect& rRect, const Rect& rect) {
  return ContainsPoint(rRect, {rect.left, rect.top}) &&
         ContainsPoint(rRect, {rect.right, rect.top}) &&
         ContainsPoint(rRect, {rect.right, rect.bottom}) &&
         ContainsPoint(rRect, {rect.left, rect.bottom});
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
  const auto horizArea = (innerBounds.width() - dw) * innerBounds.height();
  const auto vertArea = (innerBounds.height() - dh) * innerBounds.width();
  // The maximum inscribed rectangle at the corner ellipse has a corner at
  // sqrt(2)/2 * (rX, rY). Scale all corner shifts by (1 - sqrt(2)/2) to get the safe inset.
  // Add a small epsilon so the shifted corners stay safely inside the curves.
  static constexpr float diagInsetScale = (1.0f - 0.70710678118f) + 1e-5f;
  const auto innerArea =
      (innerBounds.width() - diagInsetScale * dw) * (innerBounds.height() - diagInsetScale * dh);

  if (horizArea > vertArea && horizArea > innerArea) {
    innerBounds.left += leftShift;
    innerBounds.right -= rightShift;
  } else if (vertArea > innerArea) {
    innerBounds.top += topShift;
    innerBounds.bottom -= bottomShift;
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
  dst.setRectRadii(newRect, radii);
  return dst;
}

}  // namespace tgfx
