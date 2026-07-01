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

namespace tgfx {

// Corner indices match RRect::radii() ordering: 0=TL, 1=TR, 2=BR, 3=BL.
static constexpr int kTL = 0;
static constexpr int kTR = 1;
static constexpr int kBR = 2;
static constexpr int kBL = 3;

static inline Point GetCorner(const Rect& r, int corner) {
  switch (corner) {
    case kTL:
      return {r.left, r.top};
    case kTR:
      return {r.right, r.top};
    case kBR:
      return {r.right, r.bottom};
    case kBL:
      return {r.left, r.bottom};
    default:
      return {};
  }
}

// Returns true when point a lies "inside" point b relative to the given corner direction.
static inline bool InsideCorner(int corner, const Point& a, const Point& b) {
  switch (corner) {
    case kTL:
      return a.x >= b.x && a.y >= b.y;
    case kTR:
      return a.x <= b.x && a.y >= b.y;
    case kBR:
      return a.x <= b.x && a.y <= b.y;
    case kBL:
      return a.x >= b.x && a.y <= b.y;
    default:
      return false;
  }
}

// Mirror of SkRRect::checkCornerContainment: tests whether (x, y) is inside the rRect's
// rounded boundary, assuming the point already lies inside its bounding rect.
static bool CheckCornerContainment(const RRect& rr, float x, float y) {
  const auto& rect = rr.rect();
  const auto& radii = rr.radii();
  Point canonical = {};
  int index = 0;

  if (rr.type() == RRect::Type::Oval) {
    canonical = {x - rect.centerX(), y - rect.centerY()};
    index = kTL;  // any corner works in this case
  } else {
    if (x < rect.left + radii[kTL].x && y < rect.top + radii[kTL].y) {
      index = kTL;
      canonical = {x - (rect.left + radii[kTL].x), y - (rect.top + radii[kTL].y)};
    } else if (x < rect.left + radii[kBL].x && y > rect.bottom - radii[kBL].y) {
      index = kBL;
      canonical = {x - (rect.left + radii[kBL].x), y - (rect.bottom - radii[kBL].y)};
    } else if (x > rect.right - radii[kTR].x && y < rect.top + radii[kTR].y) {
      index = kTR;
      canonical = {x - (rect.right - radii[kTR].x), y - (rect.top + radii[kTR].y)};
    } else if (x > rect.right - radii[kBR].x && y > rect.bottom - radii[kBR].y) {
      index = kBR;
      canonical = {x - (rect.right - radii[kBR].x), y - (rect.bottom - radii[kBR].y)};
    } else {
      // Not in any of the rounded corners.
      return true;
    }
  }

  // Implicit ellipse test: b^2 * x^2 + a^2 * y^2 <= (a*b)^2.
  const float rx = radii[static_cast<size_t>(index)].x;
  const float ry = radii[static_cast<size_t>(index)].y;
  const float dist = canonical.x * canonical.x * ry * ry + canonical.y * canonical.y * rx * rx;
  const float limit = (rx * ry) * (rx * ry);
  return dist <= limit;
}

// Computes the intersection corner radii at the given corner of `intersection`, derived from
// inputs a / b. Returns false when the intersection cannot be exactly represented as an RRect at
// that corner.
static bool GetIntersectionRadii(const RRect& a, const RRect& b, const Rect& intersectionRect,
                                 int corner, Point* outRadii) {
  const Point test = GetCorner(intersectionRect, corner);
  const Point aCorner = GetCorner(a.rect(), corner);
  const Point bCorner = GetCorner(b.rect(), corner);

  if (test == aCorner && test == bCorner) {
    // Both rrects share this corner anchor. Pick the dominant one.
    const Point aRadii = a.radii()[static_cast<size_t>(corner)];
    const Point bRadii = b.radii()[static_cast<size_t>(corner)];
    if (aRadii.x >= bRadii.x && aRadii.y >= bRadii.y) {
      *outRadii = aRadii;
      return true;
    }
    if (bRadii.x >= aRadii.x && bRadii.y >= aRadii.y) {
      *outRadii = bRadii;
      return true;
    }
    return false;
  }
  if (test == aCorner) {
    const Point aRadii = a.radii()[static_cast<size_t>(corner)];
    const Point bRadii = b.radii()[static_cast<size_t>(corner)];
    *outRadii = aRadii;
    if (aRadii == bRadii) {
      return InsideCorner(corner, aCorner, bCorner);
    }
    return CheckCornerContainment(b, aCorner.x, aCorner.y);
  }
  if (test == bCorner) {
    const Point aRadii = a.radii()[static_cast<size_t>(corner)];
    const Point bRadii = b.radii()[static_cast<size_t>(corner)];
    *outRadii = bRadii;
    if (aRadii == bRadii) {
      return InsideCorner(corner, bCorner, aCorner);
    }
    return CheckCornerContainment(a, bCorner.x, bCorner.y);
  }
  // Corner formed by two straight edges of a and b: both must contain the point.
  *outRadii = {0, 0};
  return CheckCornerContainment(a, test.x, test.y) && CheckCornerContainment(b, test.x, test.y);
}

// Returns true if any adjacent pair of radii along a side exceeds that side's length.
static bool RadiiOverflow(const Rect& rect, const std::array<Point, 4>& radii) {
  const float w = rect.width();
  const float h = rect.height();
  return (radii[kTL].x + radii[kTR].x > w) || (radii[kBL].x + radii[kBR].x > w) ||
         (radii[kTL].y + radii[kBL].y > h) || (radii[kTR].y + radii[kBR].y > h);
}

std::optional<RRect> RRectUtils::ConservativeIntersect(const RRect& a, const RRect& b) {
  Rect intersection = a.rect();
  if (!intersection.intersect(b.rect())) {
    // Bounding rects do not overlap; return an explicit empty RRect.
    auto empty = RRect();
    empty.setRectXY(Rect::MakeEmpty(), 0, 0);
    return empty;
  }

  std::array<Point, 4> radii = {};
  for (int corner = 0; corner < 4; ++corner) {
    if (!GetIntersectionRadii(a, b, intersection, corner, &radii[static_cast<size_t>(corner)])) {
      return std::nullopt;
    }
  }

  // If the per-side adjacent radii would need scaling to fit, the intersection cannot be
  // exactly represented as an RRect.
  if (RadiiOverflow(intersection, radii)) {
    return std::nullopt;
  }

  // All radii from valid RRects are non-negative and finite, so setRectRadii will not change them.
  RRect result = {};
  result.setRectRadii(intersection, radii);
  return result;
}

bool RRectUtils::ContainsPoint(const RRect& rRect, const Point& point) {
  return rRect.rect().contains(point.x, point.y) && CheckCornerContainment(rRect, point.x, point.y);
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
  static constexpr float kScale = (1.0f - 0.70710678118f) + 1e-5f;
  const auto innerArea = (innerBounds.width() - kScale * dw) * (innerBounds.height() - kScale * dh);

  if (horizArea > vertArea && horizArea > innerArea) {
    innerBounds.left += leftShift;
    innerBounds.right -= rightShift;
  } else if (vertArea > innerArea) {
    innerBounds.top += topShift;
    innerBounds.bottom -= bottomShift;
  } else if (innerArea > 0.f) {
    innerBounds.left += kScale * leftShift;
    innerBounds.right -= kScale * rightShift;
    innerBounds.top += kScale * topShift;
    innerBounds.bottom -= kScale * bottomShift;
  } else {
    return Rect::MakeEmpty();
  }

  return innerBounds;
}

// Corner index convention matches RRect::radii() layout: 0=TL, 1=TR, 2=BR, 3=BL.
std::optional<RRect> RRectUtils::TryAxisAlignedTransform(const RRect& src, const Matrix& matrix) {
  if (matrix.isIdentity()) {
    return src;
  }
  if (!matrix.rectStaysRect()) {
    return std::nullopt;
  }

  Rect newRect = matrix.mapRect(src.rect());
  // mapRect produces a sorted rect under axis-aligned matrices, so an empty rect indicates
  // a collapsed dimension (loss of precision). Non-finite results indicate overflow.
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

  std::array<Point, 4> radii = src.radii();
  float xScale = matrix.getScaleX();
  float yScale = matrix.getScaleY();

  // 90 / 270 degree rotation: scale entries are zero and skew entries carry the rotation.
  // 180 degrees rotations are simply flipX with a flipY and would come under a scale transform.
  if (!matrix.isScaleTranslate()) {
    const bool isClockwise = matrix.getSkewX() < 0;
    yScale = matrix.getSkewY() * (isClockwise ? 1.0f : -1.0f);
    xScale = matrix.getSkewX() * (isClockwise ? -1.0f : 1.0f);

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
