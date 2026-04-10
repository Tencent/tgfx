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

static void FlushToZero(float& a, float& b) {
  if (a + b == a) {
    b = 0;
  }
  if (a + b == b) {
    a = 0;
  }
}

// W3C CSS Background 5.5 "Overlapping Curves" algorithm.
// Uses double precision to avoid float precision loss when large and small radii coexist.
static void ScaleRadii(RRect* rRect) {
  auto width = static_cast<double>(rRect->rect.width());
  auto height = static_cast<double>(rRect->rect.height());
  auto& radii = rRect->radii;

  double scale = 1.0;
  // Top edge: TL.x + TR.x <= width
  auto sum = static_cast<double>(radii[0].x) + static_cast<double>(radii[1].x);
  if (sum > 0 && width / sum < scale) {
    scale = width / sum;
  }
  // Right edge: TR.y + BR.y <= height
  sum = static_cast<double>(radii[1].y) + static_cast<double>(radii[2].y);
  if (sum > 0 && height / sum < scale) {
    scale = height / sum;
  }
  // Bottom edge: BR.x + BL.x <= width
  sum = static_cast<double>(radii[2].x) + static_cast<double>(radii[3].x);
  if (sum > 0 && width / sum < scale) {
    scale = width / sum;
  }
  // Left edge: BL.y + TL.y <= height
  sum = static_cast<double>(radii[3].y) + static_cast<double>(radii[0].y);
  if (sum > 0 && height / sum < scale) {
    scale = height / sum;
  }

  if (scale < 1.0) {
    for (auto& r : radii) {
      r.x = static_cast<float>(static_cast<double>(r.x) * scale);
      r.y = static_cast<float>(static_cast<double>(r.y) * scale);
    }
  }

  // Flush-to-zero: when a + b == a due to float precision, force b = 0.
  FlushToZero(radii[0].x, radii[1].x);  // top
  FlushToZero(radii[1].y, radii[2].y);  // right
  FlushToZero(radii[2].x, radii[3].x);  // bottom
  FlushToZero(radii[3].y, radii[0].y);  // left

  for (auto& r : radii) {
    if (r.x <= 0 || r.y <= 0) {
      r = {0, 0};
    }
  }
}

static RRect::Type ComputeType(const Rect& rect, const std::array<Point, 4>& radii) {
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
    auto halfWidth = rect.width() * 0.5f;
    auto halfHeight = rect.height() * 0.5f;
    if (radii[0].x >= halfWidth && radii[0].y >= halfHeight) {
      return RRect::Type::Oval;
    }
    return RRect::Type::Simple;
  }
  return RRect::Type::Complex;
}

bool RRect::isRect() const {
  return type == Type::Rect;
}

bool RRect::isOval() const {
  return type == Type::Oval;
}

bool RRect::isSimple() const {
  return type == Type::Simple;
}

bool RRect::isComplex() const {
  return type == Type::Complex;
}

void RRect::setRectXY(const Rect& r, float radiusX, float radiusY) {
  rect = r.makeSorted();
  if (radiusX < 0 || radiusY < 0) {
    radiusX = radiusY = 0;
  }
  if (r.width() < radiusX + radiusX || r.height() < radiusY + radiusY) {
    auto scale = std::min(r.width() / (radiusX + radiusX), r.height() / (radiusY + radiusY));
    radiusX *= scale;
    radiusY *= scale;
  }
  auto point = Point{radiusX, radiusY};
  radii = {point, point, point, point};
  if (radiusX == 0 && radiusY == 0) {
    type = Type::Rect;
  } else if (radiusX >= rect.width() * 0.5f && radiusY >= rect.height() * 0.5f) {
    type = Type::Oval;
  } else {
    type = Type::Simple;
  }
}

void RRect::setRectRadii(const Rect& r, const std::array<Point, 4>& cornerRadii) {
  rect = r.makeSorted();
  radii = cornerRadii;
  for (auto& rad : radii) {
    if (rad.x < 0 || rad.y < 0) {
      rad = {0, 0};
    }
  }
  ScaleRadii(this);
  type = ComputeType(rect, radii);
}

void RRect::setOval(const Rect& oval) {
  rect = oval.makeSorted();
  auto point = Point{rect.width() / 2, rect.height() / 2};
  radii = {point, point, point, point};
  type = Type::Oval;
}

void RRect::scale(float scaleX, float scaleY) {
  rect.scale(scaleX, scaleY);
  for (auto& r : radii) {
    r.x *= scaleX;
    r.y *= scaleY;
  }
}
}  // namespace tgfx
