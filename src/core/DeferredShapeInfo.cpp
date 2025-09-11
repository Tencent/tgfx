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

#include "DeferredShapeInfo.h"
#include <utility>
#include "core/shapes/MatrixShape.h"
#include "core/shapes/StrokeShape.h"
#include "core/utils/ApplyStrokeToBounds.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {

std::shared_ptr<DeferredShapeInfo> DeferredShapeInfo::Make(std::shared_ptr<Shape> shape,
                                                           const Stroke* stroke, Matrix matrix) {
  if (shape == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<DeferredShapeInfo>(
      new DeferredShapeInfo(std::move(shape), stroke, matrix));
}

DeferredShapeInfo::DeferredShapeInfo(std::shared_ptr<Shape> shape, const Stroke* inputStroke,
                                     Matrix matrix)
    : _matrix(matrix) {
  if (shape->type() == Shape::Type::Matrix) {
    auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
    _matrix = matrix * matrixShape->matrix;
    _shape = matrixShape->shape;
  } else {
    _shape = std::move(shape);
  }
  if (inputStroke) {
    stroke = *inputStroke;
  }
}

void DeferredShapeInfo::applyMatrix(const Matrix& m) {
  _matrix = m * _matrix;
}

Matrix DeferredShapeInfo::matrix() const {
  return _matrix;
}

void DeferredShapeInfo::setMatrix(const Matrix& m) {
  _matrix = m;
}

std::shared_ptr<const Shape> DeferredShapeInfo::shape() const {
  return _shape;
}

Rect DeferredShapeInfo::getBounds() const {
  auto bounds = _shape->getBounds();
  if (stroke.has_value()) {
    ApplyStrokeToBounds(*stroke, &bounds, true);
  }
  bounds = _matrix.mapRect(bounds);
  return bounds;
}

UniqueKey DeferredShapeInfo::getUniqueKey() const {
  auto key = _shape->getUniqueKey();
  if (stroke.has_value()) {
    key = StrokeShape::MakeUniqueKey(key, *stroke);
  }
  key = MatrixShape::MakeUniqueKey(key, _matrix);
  return key;
}

Path DeferredShapeInfo::getPath() const {
  auto finalPath = _shape->getPath();
  if (!stroke.has_value()) {
    finalPath.transform(_matrix);
    return finalPath;
  }
  if (stroke->isHairline()) {
    finalPath.transform(_matrix);
    Stroke hairlineStroke = *stroke;
    hairlineStroke.width = 1.f;
    // hairlineStroke.applyToPath(&finalPath, _matrix.getMaxScale());
    hairlineStroke.applyToPath(&finalPath);
    return finalPath;
  }
  // stroke->applyToPath(&finalPath, _matrix.getMaxScale());
  stroke->applyToPath(&finalPath);
  finalPath.transform(_matrix);
  return finalPath;
}

}  // namespace tgfx