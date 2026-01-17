/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "PointUtils.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Point.h"

namespace tgfx {

constexpr int FloatSign(float x) {
  return (0.0f < x) - (x < 0.0f);
}

float PointUtils::DistanceToLineBetweenSquared(const Point& point, const Point& linePointA,
                                               const Point& linePointB, Side* side) {
  auto u = linePointB - linePointA;
  auto v = point - linePointA;

  float lengthSqd = LengthSquared(u);
  float det = Point::CrossProduct(u, v);
  if (side) {
    *side = static_cast<Side>(FloatSign(det));
  }
  float temp = det / lengthSqd;
  temp *= det;
  if (!FloatsAreFinite(&temp, 1)) {
    return LengthSquared(v);
  }
  return temp;
}

bool PointUtils::SetLength(Point& point, float length) {
  float currentLengthSqd = LengthSquared(point);
  if (currentLengthSqd == 0.0f) {
    return false;
  }
  float scale = std::sqrt(length * length / currentLengthSqd);
  point.x *= scale;
  point.y *= scale;
  return true;
}

}  // namespace tgfx