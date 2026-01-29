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

#pragma once

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class Matrix3DUtils {
 public:
  /**
   * Checks if any vertex of the rect is behind the camera after applying the 3D transformation.
   * A vertex is considered behind the camera when w <= 0, where w = 0 means the vertex is at the
   * camera plane (infinitely far), which is also treated as behind.
   * @param rect The rect in local coordinate system to be checked.
   * @param matrix The 3D transformation matrix containing camera and projection information.
   */
  static bool IsRectBehindCamera(const Rect& rect, const Matrix3D& matrix);

  /**
   * Returns an adapted transformation matrix for a new coordinate system established at the
   * specified point. The original matrix defines a transformation in a coordinate system with the
   * origin (0, 0) as the anchor point. When establishing a new coordinate system at an arbitrary
   * point within this space, this function computes the equivalent matrix that produces the same
   * visual transformation effect in the new coordinate system.
   * @param matrix3D The original transformation matrix defined with the origin (0, 0) as the anchor
   * point.
   * @param newOrigin The point at which to establish the new coordinate system as its origin.
   */
  static Matrix3D OriginAdaptedMatrix3D(const Matrix3D& matrix3D, const Point& newOrigin);

  /**
   * Determines if the 4x4 matrix contains only 2D affine transformations, i.e., no Z-axis related
   * transformations or projection transformations.
   */
  static bool IsMatrix3DAffine(const Matrix3D& matrix);

  /**
   * Converts a 4x4 matrix to a 3x3 matrix by extracting the 2D projection components. The z-axis
   * related transformations are ignored.
   *
   * Given a 4x4 matrix:
   *      | m00 m01 m02 m03 |
   *      | m10 m11 m12 m13 |
   *      | m20 m21 m22 m23 |
   *      | m30 m31 m32 m33 |
   *
   * The resulting 3x3 matrix is:
   *      | m00 m01 m03 |
   *      | m10 m11 m13 |
   *      | m30 m31 m33 |
   */
  static Matrix GetMayLossyMatrix(const Matrix3D& matrix);

  /**
   * Converts a 4x4 matrix to a 2D affine transformation matrix by extracting the X/Y translation
   * and scale/skew components. The z-axis related transformations and projection transformations
   * are ignored.
   *
   * Given a 4x4 matrix:
   *      | m00 m01 m02 m03 |
   *      | m10 m11 m12 m13 |
   *      | m20 m21 m22 m23 |
   *      | m30 m31 m32 m33 |
   *
   * The resulting 3x3 affine matrix is:
   *      | m00 m01 m03 |
   *      | m10 m11 m13 |
   *      |  0   0   1  |
   */
  static Matrix GetMayLossyAffineMatrix(const Matrix3D& matrix);

  /**
   * Inverse maps a rect through the matrix. Returns an empty rect if the matrix is not invertible.
   */
  static Rect InverseMapRect(const Rect& rect, const Matrix3D& matrix);

  /**
   * Adjusts a 3D transformation matrix so that the projection result can be correctly scaled.
   * This ensures the visual effect of "project first, then scale" rather than "scale first, then
   * project", which would cause incorrect perspective effects.
   * @param matrix The original 3D transformation matrix.
   * @param scale The scale factor to apply to the projection result.
   */
  static Matrix3D ScaleAdaptedMatrix3D(const Matrix3D& matrix, float scale);
};

}  // namespace tgfx
