/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/RRect.h"
#include <algorithm>

namespace tgfx {

// Zeros out the smaller of two values when it is fully absorbed by the larger one in float
// addition (a + b == a).
static inline void FlushToZero(float& a, float& b) {
  if (a + b == a) {
    b = 0;
  }
  if (a + b == b) {
    a = 0;
  }
}

// If a + b exceeds limit, lowers scale to limit / (a + b) (unless the current scale is already
// smaller). Uses double so a small value is not absorbed by a much larger one, which would hide
// the fact that their sum exceeds the limit.
static inline void UpdateMinScale(float a, float b, double limit, double& scale) {
  const auto sum = static_cast<double>(a) + static_cast<double>(b);
  if (sum > 0 && limit / sum < scale) {
    scale = limit / sum;
  }
}

// Scales smoothness so that no two adjacent smoothed corners overflow the side they share.
static inline void ScaleSmoothness(const Rect& rect, const std::array<Point, 4>& radii,
                                   float& smoothness) {
  smoothness = std::clamp(smoothness, 0.0f, 1.0f);
  // std::clamp keeps NaN unchanged, so zero it out with the comparison as well.
  if (!(smoothness > 0.0f)) {
    smoothness = 0.0f;
    return;
  }

  const auto width = static_cast<double>(rect.width());
  const auto height = static_cast<double>(rect.height());
  // (1 + s) * (a + b) <= limit bounds 1 + s the same way a radii scale is bounded.
  double scale = 1.0 + smoothness;
  UpdateMinScale(radii[0].x, radii[1].x, width, scale);
  UpdateMinScale(radii[1].y, radii[2].y, height, scale);
  UpdateMinScale(radii[2].x, radii[3].x, width, scale);
  UpdateMinScale(radii[3].y, radii[0].y, height, scale);
  smoothness = static_cast<float>(std::max(scale - 1.0, 0.0));
}

// Scales per-corner radii so that no two adjacent radii overflow the side they share.
static inline void ScaleRadii(const Rect& rect, std::array<Point, 4>& radii) {
  const auto width = static_cast<double>(rect.width());
  const auto height = static_cast<double>(rect.height());

  double scale = 1.0;
  UpdateMinScale(radii[0].x, radii[1].x, width, scale);
  UpdateMinScale(radii[1].y, radii[2].y, height, scale);
  UpdateMinScale(radii[2].x, radii[3].x, width, scale);
  UpdateMinScale(radii[3].y, radii[0].y, height, scale);

  if (scale < 1.0) {
    // Note: double-to-float conversion plus float addition may leave the sum of two adjacent
    // radii slightly greater than the side length (1-2 ULPs). Downstream rendering does not rely
    // on the strict a + b <= limit invariant, so we do not correct it here.
    for (auto& r : radii) {
      r.x = static_cast<float>(static_cast<double>(r.x) * scale);
      r.y = static_cast<float>(static_cast<double>(r.y) * scale);
    }
  }

  // After scaling, the smaller of two adjacent radii may be fully absorbed by the larger one
  // (a + b == a in float). Downstream geometry can still read b on its own as a non-zero value,
  // which disagrees with the sum result and produces seams at the boundary.
  FlushToZero(radii[0].x, radii[1].x);
  FlushToZero(radii[1].y, radii[2].y);
  FlushToZero(radii[2].x, radii[3].x);
  FlushToZero(radii[3].y, radii[0].y);

  // If either component of a corner is zero, the corner is effectively square: the other
  // component can no longer describe a curve.
  for (auto& r : radii) {
    if (r.x <= 0 || r.y <= 0) {
      r = {0, 0};
    }
  }
}

static inline RRect::Type ComputeType(const Rect& rect, const std::array<Point, 4>& radii) {
  if (rect.isEmpty()) {
    return RRect::Type::Rect;
  }
  auto allZero = true;
  for (const auto& r : radii) {
    if (r.x != 0 || r.y != 0) {
      allZero = false;
      break;
    }
  }
  if (allZero) {
    return RRect::Type::Rect;
  }
  if (radii[0] == radii[1] && radii[1] == radii[2] && radii[2] == radii[3]) {
    const auto halfWidth = rect.width() * 0.5f;
    const auto halfHeight = rect.height() * 0.5f;
    // Use strict comparison instead of an epsilon tolerance: RRect lives in logical space that
    // may later be rescaled by an arbitrary transform, so any tolerance chosen here has no
    // stable pixel-level meaning after the transform.
    if (radii[0].x >= halfWidth && radii[0].y >= halfHeight) {
      return RRect::Type::Oval;
    }
    return RRect::Type::Simple;
  }
  return RRect::Type::Complex;
}

void RRect::setRect(const Rect& rect) {
  _rect = rect.makeSorted();
  _radii = {};
  _smoothness = 0.0f;
  _type = Type::Rect;
}

void RRect::setRectXY(const Rect& rect, float radiusX, float radiusY, float smoothness) {
  const auto radius = Point{radiusX, radiusY};
  setRectRadii(rect, {radius, radius, radius, radius}, smoothness);
}

void RRect::setRectRadii(const Rect& rect, const std::array<Point, 4>& radii, float smoothness) {
  _rect = rect.makeSorted();
  _radii = radii;
  _smoothness = smoothness;
  for (auto& rad : _radii) {
    if (rad.x < 0 || rad.y < 0) {
      rad = {0, 0};
    }
  }
  // Runs before ScaleRadii: radii that do not fit their edges yield a smoothness of exactly zero,
  // while the scaled radii fit their edges only up to rounding error.
  ScaleSmoothness(_rect, _radii, _smoothness);
  ScaleRadii(_rect, _radii);
  _type = ComputeType(_rect, _radii);
  if (_type == Type::Rect || _type == Type::Oval) {
    _smoothness = 0.0f;
  }
}

void RRect::setOval(const Rect& oval) {
  const auto sorted = oval.makeSorted();
  const auto radius = Point{sorted.width() / 2, sorted.height() / 2};
  setRectRadii(sorted, {radius, radius, radius, radius});
}

// Positive scaling preserves all RRect invariants (adjacent-radii fit, smoothed-corner fit,
// Oval/Simple/Complex/Rect thresholds), so ScaleSmoothness, ScaleRadii and ComputeType are
// intentionally skipped here. Non-positive factors are out of contract.
void RRect::scale(float scaleX, float scaleY) {
  _rect.scale(scaleX, scaleY);
  for (auto& r : _radii) {
    r.x *= scaleX;
    r.y *= scaleY;
  }
}

void RRect::offset(float dx, float dy) {
  _rect.offset(dx, dy);
}

}  // namespace tgfx
