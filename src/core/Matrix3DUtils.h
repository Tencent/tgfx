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
   * When a 4x4 matrix does not contain Z-axis related transformations and projection
   * transformations, this function returns an equivalent 2D affine transformation. Otherwise, the
   * return value will lose information about Z-axis related transformations and projection
   * transformations.
   */
  static Matrix GetMayLossyAffineMatrix(const Matrix3D& matrix);

  /**
   * Inverse maps a rect through the matrix. Returns an empty rect if the matrix is not invertible.
   */
  static Rect InverseMapRect(const Rect& rect, const Matrix3D& matrix);
};

}  // namespace tgfx
