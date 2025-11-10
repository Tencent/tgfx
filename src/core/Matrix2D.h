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

#pragma once
#include "tgfx/core/Rect.h"
#include "tgfx/core/Vec.h"

namespace tgfx {

/**
 * Matrix2D holds a 3x3 matrix for transforming coordinates in 2D space. This allows mapping points
 * and vectors with translation, scaling, skewing, rotation, and perspective. These types of
 * transformations are collectively known as projective transformations. Projective transformations
 * preserve the straightness of lines but do not preserve parallelism, so parallel lines may not
 * remain parallel after transformation.
 * The elements of Matrix2D are in column-major order.
 * Matrix2D does not have a default constructor, so it must be explicitly initialized.
 */
class Matrix2D {
 public:
  /**
   * Creates a Matrix2D set to the identity matrix. The created Matrix3D is:
   *
   *       | 1 0 0 |
   *       | 0 1 0 |
   *       | 0 0 1 |
   */
  constexpr Matrix2D() : Matrix2D(1, 0, 0, 0, 1, 0, 0, 0, 1) {
  }

  /**
  * Creates a Matrix2D with the given elements. The created Matrix2D is:
  *
  *       | m00  m01  m02 |
  *       | m10  m11  m12 |
  *       | m20  m21  m22 |
  */
  static Matrix2D MakeAll(float m00, float m01, float m02, float m10, float m11, float m12,
                          float m20, float m21, float m22) {
    return {m00, m01, m02, m10, m11, m12, m20, m21, m22};
  }

  /**
   * Calculates the inverse of the current matrix and stores the result in the Matrix2D object
   * pointed to by inverse.
   * @param inverse Pointer to the Matrix2D object used to store the inverse matrix. If nullptr,
   * the method will only check invertibility and not store the result. If the current matrix is not
   * invertible, inverse will not be modified.
   * @return Returns true if the inverse matrix exists; otherwise, returns false.
   */
  bool invert(Matrix2D* inverse = nullptr) const;

  /**
   * Maps a rectangle using this matrix.
   * If the matrix contains a perspective transformation, each corner of the rectangle is mapped as a
   * 4D point (x, y, 0, 1), and the resulting rectangle is computed from the projected points
   * (after perspective division).
   */
  Rect mapRect(const Rect& src) const;

 private:
  constexpr Matrix2D(float m00, float m01, float m02, float m10, float m11, float m12, float m20,
                     float m21, float m22)
      : values{m00, m01, m02, m10, m11, m12, m20, m21, m22} {
  }

  /**
   * Maps a 2D point using this matrix.
   * The returned result is the coordinate after perspective division.
   */
  Vec2 mapVec2(const Vec2& v) const;

  /**
   * Maps a 2D point (x, y, w) using this matrix.
   * If the current matrix contains a perspective transformation, the returned Vec3 is not
   * perspective-divided; i.e., the z component of the result may not be 1.
   */
  Vec3 mapPoint(float x, float y, float w) const;

  Vec3 getCol(int i) const {
    Vec3 v;
    memcpy(&v, values + i * 3, sizeof(Vec3));
    return v;
  }

  float values[9] = {.0f};
};

}  // namespace tgfx