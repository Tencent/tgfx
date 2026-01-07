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

#include "VectorContext.h"

namespace tgfx {

void VectorContext::addShape(std::shared_ptr<Shape> shape) {
  if (shape == nullptr) {
    return;
  }
  geometries.push_back(std::make_unique<ShapeGeometry>(std::move(shape)));
  matrices.push_back(Matrix::I());
}

void VectorContext::addTextBlob(std::shared_ptr<TextBlob> blob, const Point& position) {
  if (blob == nullptr) {
    return;
  }
  geometries.push_back(std::make_unique<TextGeometry>(std::move(blob)));
  matrices.push_back(Matrix::MakeTrans(position.x, position.y));
}

std::vector<ShapeGeometry*> VectorContext::getShapeGeometries() {
  std::vector<ShapeGeometry*> result = {};
  for (auto& geometry : geometries) {
    if (geometry->type() == GeometryType::Shape) {
      result.push_back(static_cast<ShapeGeometry*>(geometry.get()));
    } else {
      auto shapeGeometry = std::make_unique<ShapeGeometry>(geometry->getShape());
      result.push_back(shapeGeometry.get());
      geometry = std::move(shapeGeometry);
    }
  }
  return result;
}

}  // namespace tgfx
