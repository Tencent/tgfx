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
#include <algorithm>
#include <cmath>
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
  return Matrix::MakeAll(matrix.getRowColumn(0, 0), matrix.getRowColumn(0, 1),
                         matrix.getRowColumn(0, 3), matrix.getRowColumn(1, 0),
                         matrix.getRowColumn(1, 1), matrix.getRowColumn(1, 3));
}

Rect Matrix3DUtils::InverseMapRect(const Rect& rect, const Matrix3D& matrix) {
  auto matrix2D = matrix.asMatrix();
  Matrix inversedMatrix;
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

Point Matrix3DUtils::ProjectionDensity(const Matrix3D& matrix, const Rect& rect) {
  const Point samples[5] = {
      {rect.centerX(), rect.centerY()},   {rect.left, rect.top},   {rect.right, rect.top},
      {rect.right, rect.bottom},          {rect.left, rect.bottom}};
  // Finite-difference step scaled to the local extent keeps the Jacobian estimate well
  // conditioned regardless of how large or small the rect is in local units.
  const float extent = std::min(rect.width(), rect.height());
  const float step = std::max(1e-3f, extent * 1e-3f);
  float maxDx = 0.0f;
  float maxDy = 0.0f;
  for (const auto& sample : samples) {
    const auto center = matrix.mapPoint(Vec3(sample.x, sample.y, 0.0f));
    const auto shiftedX = matrix.mapPoint(Vec3(sample.x + step, sample.y, 0.0f));
    const auto shiftedY = matrix.mapPoint(Vec3(sample.x, sample.y + step, 0.0f));
    const float dxx = (shiftedX.x - center.x) / step;
    const float dxy = (shiftedX.y - center.y) / step;
    const float dyx = (shiftedY.x - center.x) / step;
    const float dyy = (shiftedY.y - center.y) / step;
    maxDx = std::max(maxDx, std::sqrt(dxx * dxx + dxy * dxy));
    maxDy = std::max(maxDy, std::sqrt(dyx * dyx + dyy * dyy));
  }
  return Point::Make(maxDx, maxDy);
}

}  // namespace tgfx
