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

#include "MatrixShape.h"
#include "core/shapes/MergeShape.h"
#include "core/shapes/StrokeShape.h"
#include "core/utils/StrokeUtils.h"
#include "core/utils/Types.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"

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

Path MatrixShape::getPath(const Matrix& scaleMatrix) const {
  auto totalMatrix = scaleMatrix * matrix;

  bool isHairline = false;
  std::shared_ptr<StrokeShape> originStrokeShape = nullptr;
  if (shape->type() == Type::Stroke) {
    // Handle hairline stroke
    auto strokeShape = std::static_pointer_cast<StrokeShape>(shape);
    if (strokeShape->stroke.isHairline() ||
        TreatStrokeAsHairline(strokeShape->stroke, totalMatrix)) {
      isHairline = true;
      originStrokeShape = strokeShape;
    }
  } else if (shape->type() == Type::Merge) {
    // Handle cases where inner or outer strokes are hairline
    auto mergeShape = std::static_pointer_cast<MergeShape>(shape);
    if (mergeShape->pathOp == PathOp::Intersect || mergeShape->pathOp == PathOp::Difference) {
      if (mergeShape->first->type() == Type::Stroke) {
        auto strokeShape = std::static_pointer_cast<StrokeShape>(mergeShape->first);
        if (strokeShape->shape == mergeShape->second) {
          auto stroke = strokeShape->stroke;
          stroke.width /= 2.f;
          if (strokeShape->stroke.isHairline() || TreatStrokeAsHairline(stroke, totalMatrix)) {
            isHairline = true;
            originStrokeShape = strokeShape;
          }
        }
      }
    }
  }
  if (isHairline) {
    auto hairlineStroke = originStrokeShape->stroke;
    hairlineStroke.width = 1.f;
    auto hairlinePath = originStrokeShape->shape->getPath(totalMatrix);
    hairlinePath.transform(matrix);
    hairlineStroke.applyToPath(&hairlinePath, totalMatrix.getMaxScale());
    return hairlinePath;
  }
  auto path = shape->getPath(scaleMatrix * matrix);
  path.transform(matrix);
  return path;
}

UniqueKey MatrixShape::MakeUniqueKey(const UniqueKey& key, const Matrix& matrix) {
  static const auto SingleScaleMatrixShapeType = UniqueID::Next();
  static const auto BothScalesShapeType = UniqueID::Next();
  static const auto RSXformShapeType = UniqueID::Next();
  auto hasRSXform = !matrix.isScaleTranslate();
  auto hasBothScales = hasRSXform || matrix.getScaleX() != matrix.getScaleY();
  if (!hasBothScales && matrix.getScaleX() == 1.0f) {
    // The matrix has translation only.
    return key;
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
  return UniqueKey::Append(key, bytesKey.data(), bytesKey.size());
}

UniqueKey MatrixShape::getUniqueKey() const {
  return MakeUniqueKey(shape->getUniqueKey(), matrix);
}

}  // namespace tgfx
