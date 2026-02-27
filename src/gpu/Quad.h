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

#pragma once

#include "core/utils/Log.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * Quad represents a quadrilateral with vertices in Z-order.
 *
 * Vertex layout:
 *   0(LT) --- 2(RT)
 *     |         |
 *   1(LB) --- 3(RB)
 */
class Quad {
 public:
  /**
   * Creates a Quad from a rectangle. If matrix is provided, the quad vertices are transformed.
   */
  static Quad MakeFrom(const Rect& rect, const Matrix* matrix = nullptr);

  /**
   * Creates a Quad from four points in clockwise order. If the four points form a rectangle,
   * prefer using MakeFrom(const Rect&, const Matrix*) for optimized processing.
   */
  static Quad MakeFromCW(const Point& p0, const Point& p1, const Point& p2, const Point& p3);

  const Point& point(size_t i) const {
    DEBUG_ASSERT(i < 4);
    return points[i];
  }

  /**
   * Returns true if the quad is a rectangle.
   */
  bool isRect() const {
    return _isRect;
  }

  /**
   * Transforms all four vertices by the given matrix in place.
   */
  void transform(const Matrix& matrix);

 private:
  Quad() = default;
  explicit Quad(const Rect& rect, const Matrix* matrix = nullptr);

  Point points[4] = {};
  bool _isRect = false;
};

}  // namespace tgfx
