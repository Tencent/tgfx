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

#include <vector>
#include "tgfx/core/Point.h"

namespace tgfx {

/**
 * CurveConverter provides utilities for converting between different curve representations.
 */
class CurveConverter {
 public:
  /**
   * Converts a conic curve to a series of quadratic Bezier curves.
   * @param p0     conic start point
   * @param p1     conic control point
   * @param p2     conic end point
   * @param weight conic weight
   * @param pow2   quad count as power of two (0 to 5), e.g., pow2=1 generates 2 quads
   * @return       quad control points, size = 1 + 2 * numQuads
   */
  static std::vector<Point> ConicToQuads(const Point& p0, const Point& p1, const Point& p2,
                                         float weight, int pow2 = 1);

  /**
   * Converts a conic curve to a series of cubic Bezier curves. For a standard 90-degree circular
   * arc (weight ≈ √2/2), uses optimal cubic approximation with ~0.027% error. For other cases,
   * converts via quadratic curves first, then elevates each quad to cubic.
   * @param p0     conic start point
   * @param p1     conic control point
   * @param p2     conic end point
   * @param weight conic weight
   * @param pow2   cubic count as power of two (0 to 5), e.g., pow2=1 generates 2 cubics
   * @return       cubic control points, size = 1 + 3 * numCubics
   */
  static std::vector<Point> ConicToCubics(const Point& p0, const Point& p1, const Point& p2,
                                          float weight, int pow2 = 1);
};

}  // namespace tgfx
