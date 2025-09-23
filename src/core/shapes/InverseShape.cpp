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

#include "InverseShape.h"
#include "core/shapes/MatrixShape.h"
#include "core/shapes/PathShape.h"
#include "core/shapes/StrokeShape.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::ApplyInverse(std::shared_ptr<Shape> shape) {
  if (shape == nullptr) {
    return nullptr;
  }
  switch (shape->type()) {
    case Type::Inverse: {
      return std::static_pointer_cast<InverseShape>(shape)->shape;
    }
    case Type::Path: {
      auto path = std::static_pointer_cast<PathShape>(shape)->path;
      path.toggleInverseFillType();
      return std::make_shared<PathShape>(std::move(path));
    }
    case Type::Matrix: {
      auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
      auto invertShape = Shape::ApplyInverse(matrixShape->shape);
      return std::make_shared<MatrixShape>(std::move(invertShape), matrixShape->matrix);
    }
    case Type::Stroke: {
      auto strokeShape = std::static_pointer_cast<StrokeShape>(shape);
      auto invertShape = Shape::ApplyInverse(strokeShape->shape);
      return std::make_shared<StrokeShape>(std::move(invertShape), strokeShape->stroke);
    }
    default:
      break;
  }
  return std::make_shared<InverseShape>(std::move(shape));
}

Path InverseShape::onGetPath(float resolutionScale) const {
  auto path = shape->onGetPath(resolutionScale);
  path.toggleInverseFillType();
  return path;
}
}  // namespace tgfx
