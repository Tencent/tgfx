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

#include "ShapeUtils.h"
#include "core/shapes/MatrixShape.h"
#include "core/shapes/StrokeShape.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

Path ShapeUtils::GetShapeRenderingPath(std::shared_ptr<Shape> shape, float resolutionScale) {
  if (shape == nullptr) {
    return {};
  }
  return shape->onGetPath(resolutionScale);
}

float ShapeUtils::CalculateAlphaReduceFactorIfHairline(std::shared_ptr<Shape> shape) {
  auto [strokeShape, matrix] = DecomposeStrokeShape(shape);
  if (!strokeShape || strokeShape->stroke.width <= 0.f) {
    return 1.f;
  }
  float scale = matrix.getMaxScale();
  return std::min(strokeShape->stroke.width * scale, 1.f);
}

std::tuple<std::shared_ptr<StrokeShape>, Matrix> ShapeUtils::DecomposeStrokeShape(
    std::shared_ptr<Shape> shape) {
  auto [innerShape, matrix] = UnwrapMatrixShape(std::move(shape));
  if (innerShape == nullptr || innerShape->type() != Shape::Type::Stroke) {
    return {nullptr, Matrix::I()};
  }
  return {std::static_pointer_cast<StrokeShape>(innerShape), matrix};
}

const MatrixShape* ShapeUtils::AsMatrixShape(const Shape* shape) {
  if (shape != nullptr && shape->type() == Shape::Type::Matrix) {
    return static_cast<const MatrixShape*>(shape);
  }
  return nullptr;
}

std::pair<std::shared_ptr<Shape>, Matrix> ShapeUtils::UnwrapMatrixShape(
    std::shared_ptr<Shape> shape) {
  auto matrix = Matrix::I();
  while (auto* matrixShape = AsMatrixShape(shape.get())) {
    matrix.preConcat(matrixShape->matrix);
    shape = matrixShape->shape;
  }
  return {std::move(shape), matrix};
}

}  // namespace tgfx
