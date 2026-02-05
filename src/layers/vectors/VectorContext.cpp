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
  auto geometry = std::make_unique<Geometry>();
  geometry->shape = std::move(shape);
  geometries.push_back(std::move(geometry));
}

void VectorContext::addTextBlob(std::shared_ptr<TextBlob> blob, const Point& position,
                                const std::vector<Point>& anchors) {
  if (blob == nullptr) {
    return;
  }
  auto geometry = std::make_unique<Geometry>();
  geometry->textBlob = std::move(blob);
  geometry->matrix = Matrix::MakeTrans(position.x, position.y);
  geometry->anchors = anchors;
  geometries.push_back(std::move(geometry));
}

std::vector<Geometry*> VectorContext::getShapeGeometries() {
  std::vector<Geometry*> result = {};
  result.reserve(geometries.size());
  for (auto& geometry : geometries) {
    if (geometry->hasText()) {
      geometry->convertToShape();
    }
    result.push_back(geometry.get());
  }
  return result;
}

std::vector<Geometry*> VectorContext::getGlyphGeometries() {
  std::vector<Geometry*> result = {};
  for (auto& geometry : geometries) {
    if (!geometry->hasText()) {
      continue;
    }
    if (geometry->textBlob != nullptr) {
      geometry->expandToGlyphs();
    }
    result.push_back(geometry.get());
  }
  return result;
}

}  // namespace tgfx
