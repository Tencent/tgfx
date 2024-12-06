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
#include "tgfx/core/Buffer.h"

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

std::shared_ptr<Data> Quad::toTriangleStrips() const {
  Buffer buffer(8 * sizeof(float));
  auto vertices = static_cast<float*>(buffer.data());
  int index = 0;
  for (size_t i = 4; i >= 1; --i) {
    vertices[index++] = points[i - 1].x;
    vertices[index++] = points[i - 1].y;
  }
  return buffer.release();
}

}  // namespace tgfx
