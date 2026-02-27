/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "FillTypeShape.h"
#include "core/shapes/MatrixShape.h"
#include "core/shapes/PathShape.h"

namespace tgfx {

std::shared_ptr<Shape> Shape::ApplyFillType(std::shared_ptr<Shape> shape, PathFillType fillType) {
  if (shape == nullptr || shape->fillType() == fillType) {
    return shape;
  }
  if (shape->type() == Type::Path) {
    auto path = std::static_pointer_cast<PathShape>(shape)->path;
    path.setFillType(fillType);
    return std::make_shared<PathShape>(std::move(path));
  }
  if (shape->type() == Type::FillType) {
    auto fillTypeShape = std::static_pointer_cast<FillTypeShape>(shape);
    return std::make_shared<FillTypeShape>(fillTypeShape->shape, fillType);
  }
  // Apply fill type to the inner shape of MatrixShape, so that the outer matrix can be used to
  // do some optimization.
  if (shape->type() == Type::Matrix) {
    auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
    auto innerShape = ApplyFillType(matrixShape->shape, fillType);
    return std::make_shared<MatrixShape>(std::move(innerShape), matrixShape->matrix);
  }
  return std::make_shared<FillTypeShape>(std::move(shape), fillType);
}

Path FillTypeShape::onGetPath(float resolutionScale) const {
  auto path = shape->onGetPath(resolutionScale);
  path.setFillType(_fillType);
  return path;
}
}  // namespace tgfx
