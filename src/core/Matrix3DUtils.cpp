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

#include "core/Matrix3DUtils.h"
#include "core/Matrix2D.h"
#include "utils/MathExtra.h"

namespace tgfx {

bool Matrix3DUtils::IsRectBehindCamera(const Rect& rect, const Matrix3D& matrix) {
  return matrix.mapHomogeneous(rect.left, rect.top, 0, 1).w <= 0 ||
         matrix.mapHomogeneous(rect.left, rect.bottom, 0, 1).w <= 0 ||
         matrix.mapHomogeneous(rect.right, rect.top, 0, 1).w <= 0 ||
         matrix.mapHomogeneous(rect.right, rect.bottom, 0, 1).w <= 0;
}

Matrix3D Matrix3DUtils::OriginAdaptedMatrix3D(const Matrix3D& matrix3D, const Point& newOrigin) {
  auto offsetMatrix = Matrix3D::MakeTranslate(newOrigin.x, newOrigin.y, 0);
  auto invOffsetMatrix = Matrix3D::MakeTranslate(-newOrigin.x, -newOrigin.y, 0);
  return invOffsetMatrix * matrix3D * offsetMatrix;
}

bool Matrix3DUtils::IsMatrix3DAffine(const Matrix3D& matrix) {
  return FloatNearlyZero(matrix.getRowColumn(0, 2)) && FloatNearlyZero(matrix.getRowColumn(1, 2)) &&
         matrix.getRow(2) == Vec4(0, 0, 1, 0) && matrix.getRow(3) == Vec4(0, 0, 0, 1);
}

Matrix Matrix3DUtils::GetMayLossyAffineMatrix(const Matrix3D& matrix) {
  auto affineMatrix = Matrix::I();
  affineMatrix.setAll(matrix.getRowColumn(0, 0), matrix.getRowColumn(0, 1),
                      matrix.getRowColumn(0, 3), matrix.getRowColumn(1, 0),
                      matrix.getRowColumn(1, 1), matrix.getRowColumn(1, 3));
  return affineMatrix;
}

Rect Matrix3DUtils::InverseMapRect(const Rect& rect, const Matrix3D& matrix) {
  float values[16] = {};
  matrix.getColumnMajor(values);
  auto matrix2D = Matrix2D::MakeAll(values[0], values[1], values[3], values[4], values[5],
                                    values[7], values[12], values[13], values[15]);
  Matrix2D inversedMatrix;
  if (!matrix2D.invert(&inversedMatrix)) {
    return Rect::MakeEmpty();
  }
  return inversedMatrix.mapRect(rect);
}

Matrix3D Matrix3DUtils::ScaleAdaptedMatrix3D(const Matrix3D& matrix, float scale) {
  if (FloatNearlyEqual(scale, 1.0f)) {
    return matrix;
  }
  auto invScale = 1.0f / scale;
  auto invScaleMatrix = Matrix3D::MakeScale(invScale, invScale, 1.0f);
  auto scaleMatrix = Matrix3D::MakeScale(scale, scale, 1.0f);
  return scaleMatrix * matrix * invScaleMatrix;
}

}  // namespace tgfx
