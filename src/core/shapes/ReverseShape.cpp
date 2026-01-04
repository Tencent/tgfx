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

#include "ReverseShape.h"
#include "core/shapes/MatrixShape.h"
#include "core/shapes/PathShape.h"

namespace tgfx {

std::shared_ptr<Shape> Shape::ApplyReverse(std::shared_ptr<Shape> shape) {
  if (shape == nullptr) {
    return nullptr;
  }
  if (shape->type() == Type::Path) {
    auto path = std::static_pointer_cast<PathShape>(shape)->path;
    path.reverse();
    return std::make_shared<PathShape>(std::move(path));
  }
  if (shape->type() == Type::Reverse) {
    auto reverseShape = std::static_pointer_cast<ReverseShape>(shape);
    return reverseShape->shape;
  }
  // Apply reverse to the inner shape of MatrixShape, so that the outer matrix can be used to
  // do some optimization.
  if (shape->type() == Type::Matrix) {
    auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
    auto innerShape = ApplyReverse(matrixShape->shape);
    return std::make_shared<MatrixShape>(std::move(innerShape), matrixShape->matrix);
  }
  return std::make_shared<ReverseShape>(std::move(shape));
}

Path ReverseShape::onGetPath(float resolutionScale) const {
  auto path = shape->onGetPath(resolutionScale);
  path.reverse();
  return path;
}
}  // namespace tgfx
