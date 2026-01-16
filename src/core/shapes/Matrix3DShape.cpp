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
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "Matrix3DShape.h"

namespace tgfx {

std::shared_ptr<Shape> Shape::ApplyMatrix3D(std::shared_ptr<Shape> shape, const Matrix3D& matrix) {
  if (shape == nullptr) {
    return nullptr;
  }
  if (matrix.isIdentity()) {
    return shape;
  }
  if (!matrix.invert()) {
    return nullptr;
  }
  if (shape->type() != Type::Matrix3D) {
    return std::make_shared<Matrix3DShape>(std::move(shape), matrix);
  }
  auto matrixShape = std::static_pointer_cast<Matrix3DShape>(shape);
  auto totalMatrix = matrix * matrixShape->matrix;
  if (totalMatrix.isIdentity()) {
    return matrixShape->shape;
  }
  return std::make_shared<Matrix3DShape>(matrixShape->shape, totalMatrix);
}

Rect Matrix3DShape::onGetBounds() const {
  auto bounds = shape->onGetBounds();
  auto result = matrix.mapRect(bounds);
  return result;
}

UniqueKey Matrix3DShape::MakeUniqueKey(const UniqueKey& key, const Matrix3D& matrix) {
  float values[16] = {};
  matrix.getColumnMajor(values);

  bool isTranslationOnly = true;
  for (int i = 0; i < 16; ++i) {
    if (i == 0 || i == 5 || i == 10 || i == 15) {
      if (values[i] != 1.0f) {
        isTranslationOnly = false;
        break;
      }
    } else if (i < 12) {
      if (values[i] != 0.0f) {
        isTranslationOnly = false;
        break;
      }
    }
  }

  if (isTranslationOnly) {
    return key;
  }

  // For 3D transformations, there are perspective and scaling elements such as m33. Therefore, even
  // if only the translation elements m31, m32, and m33 differ, the matrix may still produce scaling
  // effects on the Shape. As a result, all elements must be considered when generating the unique key.
  BytesKey bytesKey(16);
  for (float value : values) {
    bytesKey.write(value);
  }
  return UniqueKey::Append(key, bytesKey.data(), bytesKey.size());
}

Path Matrix3DShape::onGetPath(float resolutionScale) const {
  auto path = shape->onGetPath(resolutionScale);
  path.transform3D(matrix);
  return path;
}

UniqueKey Matrix3DShape::getUniqueKey() const {
  return MakeUniqueKey(shape->getUniqueKey(), matrix);
}

}  // namespace tgfx
