/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "MatrixShape.h"
#include "core/utils/UniqueID.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::ApplyMatrix(std::shared_ptr<Shape> shape, const Matrix& matrix) {
  if (shape == nullptr) {
    return nullptr;
  }
  if (matrix.isIdentity()) {
    return shape;
  }
  if (!matrix.invertible()) {
    return nullptr;
  }
  if (shape->type() != Type::Matrix) {
    return std::make_shared<MatrixShape>(std::move(shape), matrix);
  }
  auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
  auto totalMatrix = matrix * matrixShape->matrix;
  if (totalMatrix.isIdentity()) {
    return matrixShape->shape;
  }
  return std::make_shared<MatrixShape>(matrixShape->shape, totalMatrix);
}

Rect MatrixShape::getBounds() const {
  auto bounds = shape->getBounds();
  matrix.mapRect(&bounds);
  return bounds;
}

Path MatrixShape::getPath() const {
  auto path = shape->getPath();
  path.transform(matrix);
  return path;
}

bool MatrixShape::isLine(Point line[2]) const {
  if (!shape->isLine(line)) {
    return false;
  }
  if (line) {
    matrix.mapPoints(line, 2);
  }
  return true;
}

bool MatrixShape::isRect(Rect* rect) const {
  if (!matrix.rectStaysRect()) {
    return false;
  }
  if (!shape->isRect(rect)) {
    return false;
  }
  if (rect) {
    matrix.mapRect(rect);
  }
  return true;
}

bool MatrixShape::isOval(Rect* bounds) const {
  if (!matrix.rectStaysRect()) {
    return false;
  }
  if (!shape->isOval(bounds)) {
    return false;
  }
  if (bounds) {
    matrix.mapRect(bounds);
  }
  return true;
}

bool MatrixShape::isRRect(RRect* rRect) const {
  if (!matrix.rectStaysRect()) {
    return false;
  }
  if (!shape->isRRect(rRect)) {
    return false;
  }
  if (rRect) {
    matrix.mapRect(&rRect->rect);
    matrix.mapPoints(&rRect->radii, 1);
    if (rRect->radii.x < 0.0f) {
      rRect->radii.x = -rRect->radii.x;
    }
    if (rRect->radii.y < 0.0f) {
      rRect->radii.y = -rRect->radii.y;
    }
  }
  return true;
}

UniqueKey MatrixShape::getUniqueKey() const {
  static const auto SingleScaleMatrixShapeType = UniqueID::Next();
  static const auto BothScalesShapeType = UniqueID::Next();
  static const auto RSXformShapeType = UniqueID::Next();
  auto hasRSXform = matrix.getSkewX() != 0 || matrix.getSkewY() != 0;
  auto hasBothScales = hasRSXform || matrix.getScaleX() != matrix.getScaleY();
  if (!hasBothScales && matrix.getScaleX() == 1.0f) {
    // The matrix has translation only.
    return shape->getUniqueKey();
  }
  size_t count = 2 + (hasBothScales ? 1 : 0) + (hasRSXform ? 2 : 0);
  auto type = hasBothScales ? (hasRSXform ? RSXformShapeType : BothScalesShapeType)
                            : SingleScaleMatrixShapeType;
  BytesKey bytesKey(count);
  bytesKey.write(type);
  bytesKey.write(matrix.getScaleX());
  if (hasBothScales) {
    bytesKey.write(matrix.getScaleY());
  }
  if (hasRSXform) {
    bytesKey.write(matrix.getSkewX());
    bytesKey.write(matrix.getSkewY());
  }
  return UniqueKey::Append(shape->getUniqueKey(), bytesKey.data(), bytesKey.size());
}

}  // namespace tgfx
