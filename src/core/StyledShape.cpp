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

#include "StyledShape.h"
#include "core/shapes/MatrixShape.h"
#include "core/shapes/StrokeShape.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {

std::shared_ptr<StyledShape> StyledShape::Make(std::shared_ptr<Shape> shape, const Stroke* stroke,
                                               Matrix matrix) {
  if (shape == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<StyledShape>(new StyledShape(std::move(shape), stroke, matrix));
}

StyledShape::StyledShape(std::shared_ptr<Shape> shape, const Stroke* stroke, Matrix matrix)
    : _matrix(matrix) {
  if (stroke) {
    _stroke = *stroke;
  }
  if (shape->type() == Shape::Type::Matrix) {
    // Flatten nested MatrixShapes
    auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
    auto scales = matrixShape->matrix.getAxisScales();
    if (scales.x != scales.y || scales.x > 1.f) {
      _shape = std::move(shape);
    } else {
      _matrix = matrix * matrixShape->matrix;
      _shape = matrixShape->shape;
      if (_stroke.has_value() && !_stroke->isHairline()) {
        _stroke->width /= scales.x;
      }
    }
  } else {
    _shape = std::move(shape);
  }
}

void StyledShape::applyMatrix(const Matrix& matrix) {
  _matrix = matrix * _matrix;
}

Matrix StyledShape::matrix() const {
  return _matrix;
}

void StyledShape::setMatrix(const Matrix& matrix) {
  _matrix = matrix;
}

std::shared_ptr<const Shape> StyledShape::shape() const {
  return _shape;
}

Rect StyledShape::getBounds() const {
  auto bounds = _shape->getBounds();
  if (_stroke.has_value()) {
    ApplyStrokeToBounds(*_stroke, &bounds, true);
  }
  bounds = _matrix.mapRect(bounds);
  return bounds;
}

UniqueKey StyledShape::getUniqueKey() const {
  auto key = _shape->getUniqueKey();
  if (_stroke.has_value()) {
    key = StrokeShape::MakeUniqueKey(key, *_stroke);
  }
  key = MatrixShape::MakeUniqueKey(key, _matrix);
  return key;
}

Path StyledShape::getPath() const {
  auto finalPath = _shape->getPath();
  if (!_stroke.has_value()) {
    finalPath.transform(_matrix);
    return finalPath;
  }
  if (_stroke->isHairline()) {
    finalPath.transform(_matrix);
    Stroke hairlineStroke = *_stroke;
    hairlineStroke.width = 1.f;
    hairlineStroke.applyToPath(&finalPath, _matrix.getMaxScale());
    return finalPath;
  }
  _stroke->applyToPath(&finalPath, _matrix.getMaxScale());
  finalPath.transform(_matrix);
  return finalPath;
}

void StyledShape::convertToHairlineIfNecessary(Fill& fill) {
  float strokeWidth = 0.f;
  if (!_stroke.has_value()) {
    return;
  }
  if (!TreatStrokeAsHairline(*_stroke, _matrix, &strokeWidth)) {
    return;
  }
  fill.color.alpha *= strokeWidth / 1.f;
  _stroke->width = 0.f;
}

}  // namespace tgfx