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

#include "ShapeVertexSource.h"
#include "PathTriangulator.h"

namespace tgfx {

ShapeVertexSource::ShapeVertexSource(std::shared_ptr<Shape> shape, bool antiAlias)
    : shape(std::move(shape)), antiAlias(antiAlias) {
}

std::shared_ptr<Data> ShapeVertexSource::getData() const {
  if (shape == nullptr) {
    return nullptr;
  }

  auto path = shape->getPath();
  if (path.isEmpty()) {
    return nullptr;
  }

  std::vector<float> vertices;
  auto bounds = path.getBounds();
  size_t triangleCount = 0;

  if (antiAlias) {
    triangleCount = PathTriangulator::ToAATriangles(path, bounds, &vertices);
  } else {
    triangleCount = PathTriangulator::ToTriangles(path, bounds, &vertices);
  }

  if (triangleCount == 0) {
    return nullptr;
  }

  return Data::MakeWithCopy(vertices.data(), vertices.size() * sizeof(float));
}

}  // namespace tgfx
