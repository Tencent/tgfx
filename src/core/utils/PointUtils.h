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

#pragma once

#include "core/utils/Log.h"
#include "tgfx/core/Point.h"

namespace tgfx {

class PointUtils {
 public:
  enum class Side {
    Left = -1,
    On = 0,
    Right = 1,
  };

  static float LengthSquared(const Point& p) {
    return (p.x * p.x) + (p.y * p.y);
  }

  static bool SetLength(Point& point, float length);

  static float DistanceSquared(const Point& a, const Point& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
  }

  static float DistanceToLineBetweenSquared(const Point& point, const Point& linePointA,
                                            const Point& linePointB, Side* side = nullptr);

  static Point MakeOrthogonal(const Point& vec, Side side = Side::Left) {
    DEBUG_ASSERT((side == Side::Right || side == Side::Left));
    return (side == Side::Right) ? Point::Make(-vec.y, vec.x) : Point::Make(vec.y, -vec.x);
  }
};

}  // namespace tgfx