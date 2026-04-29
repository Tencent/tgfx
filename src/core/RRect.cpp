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

void RRect::setRectXY(const Rect& rect, float radiusX, float radiusY) {
  _rect = rect.makeSorted();
  if (radiusX < 0 || radiusY < 0) {
    radiusX = radiusY = 0;
  }
  if (_rect.width() < radiusX + radiusX || _rect.height() < radiusY + radiusY) {
    float scale =
        std::min(_rect.width() / (radiusX + radiusX), _rect.height() / (radiusY + radiusY));
    radiusX *= scale;
    radiusY *= scale;
  }
  const auto radius = Point{radiusX, radiusY};
  _radii = {radius, radius, radius, radius};
  if (radiusX <= 0 || radiusY <= 0) {
    _radii = {};
    _type = Type::Rect;
  } else if (radiusX >= _rect.width() * 0.5f && radiusY >= _rect.height() * 0.5f) {
    // Use strict comparison instead of an epsilon tolerance: RRect lives in logical space that
    // may later be rescaled by an arbitrary transform, so any tolerance chosen here has no
    // stable pixel-level meaning after the transform.
    _type = Type::Oval;
  } else {
    _type = Type::Simple;
  }
}

void RRect::setRectRadii(const Rect& rect, const std::array<Point, 4>& radii) {
  _rect = rect.makeSorted();
  _radii = radii;
  for (auto& rad : _radii) {
    if (rad.x < 0 || rad.y < 0) {
      rad = {0, 0};
    }
  }
  ScaleRadii(_rect, _radii);
  _type = ComputeType(_rect, _radii);
}

void RRect::setOval(const Rect& oval) {
  _rect = oval.makeSorted();
  if (_rect.isEmpty()) {
    _radii = {};
    _type = Type::Rect;
    return;
  }

  const auto radius = Point{_rect.width() / 2, _rect.height() / 2};
  _radii = {radius, radius, radius, radius};
  _type = Type::Oval;
}

// Positive scaling preserves all RRect classification invariants (adjacent-radii fit,
// Oval/Simple/Complex/Rect thresholds), so ScaleRadii and ComputeType are intentionally
// skipped here. Non-positive factors are out of contract.
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
