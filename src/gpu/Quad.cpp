/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

namespace tgfx {
Quad Quad::MakeFrom(const Rect& rect, const Matrix* matrix) {
  std::vector<Point> points;
  points.push_back(Point::Make(rect.left, rect.top));
  points.push_back(Point::Make(rect.left, rect.bottom));
  points.push_back(Point::Make(rect.right, rect.top));
  points.push_back(Point::Make(rect.right, rect.bottom));
  if (matrix) {
    matrix->mapPoints(points.data(), 4);
  }
  return Quad(points);
}

Rect Quad::bounds() const {
  auto min = [](const float c[4]) { return std::min(std::min(c[0], c[1]), std::min(c[2], c[3])); };
  auto max = [](const float c[4]) { return std::max(std::max(c[0], c[1]), std::max(c[2], c[3])); };
  float x[4] = {points[0].x, points[1].x, points[2].x, points[3].x};
  float y[4] = {points[0].y, points[1].y, points[2].y, points[3].y};
  return {min(x), min(y), max(x), max(y)};
}
}  // namespace tgfx
