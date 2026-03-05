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

#include "Quad.h"
#include "core/MatrixUtils.h"

namespace tgfx {

static inline bool RectStaysRect(const Matrix& matrix) {
  return matrix.rectStaysRect() || MatrixUtils::PreservesRightAngles(matrix);
}

Quad Quad::MakeFrom(const Rect& rect, const Matrix* matrix) {
  return Quad(rect, matrix);
}

Quad Quad::MakeFromCW(const Point& p0, const Point& p1, const Point& p2, const Point& p3) {
  Quad quad;
  // Convert CW to Z-order.
  quad.points[0] = p0;
  quad.points[1] = p3;
  quad.points[2] = p1;
  quad.points[3] = p2;
  return quad;
}

Quad::Quad(const Rect& rect, const Matrix* matrix) {
  points[0] = Point::Make(rect.left, rect.top);
  points[1] = Point::Make(rect.left, rect.bottom);
  points[2] = Point::Make(rect.right, rect.top);
  points[3] = Point::Make(rect.right, rect.bottom);
  if (matrix) {
    matrix->mapPoints(points, 4);
  }
  _isRect = !matrix || RectStaysRect(*matrix);
}

void Quad::transform(const Matrix& matrix) {
  if (matrix.isIdentity()) {
    return;
  }
  matrix.mapPoints(points, 4);
  if (_isRect) {
    // We don't strictly track rect through multiple transforms.
    // For example, two 45° rotations won't restore _isRect to true.
    _isRect = RectStaysRect(matrix);
  }
}

}  // namespace tgfx
