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

#include <cstring>
#include "Vec.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * Matrix3D holds a 4x4 matrix for transforming coordinates in 3D space. This allows mapping points
 * and vectors with translation, scaling, skewing, rotation, and perspective. These types of
 * transformations are collectively known as projective transformations. Projective transformations
 * preserve the straightness of lines but do not preserve parallelism, so parallel lines may not
 * remain parallel after transformation.
 * The elements of Matrix3D are in column-major order.
 * Matrix3D does not have a default constructor, so it must be explicitly initialized.
 */
class Matrix3D {
 public:
  /**
   * Creates a Matrix3D set to the identity matrix. The created Matrix3D is:
   *
   *       | 1 0 0 0 |
   *       | 0 1 0 0 |
   *       | 0 0 1 0 |
   *       | 0 0 0 1 |
   */
  constexpr Matrix3D() : Matrix3D(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) {
  }

  /**
  * Copies the matrix values into a 16-element array in column-major order.
  */
  void getColumnMajor(float buffer[16]) const {
    memcpy(buffer, values, sizeof(values));
  }

  /**
   * Returns the matrix value at the given row and column.
   * @param r  Row index, valid range 0..3.
   * @param c  Column index, valid range 0..3.
   */
  float getRowColumn(int r, int c) const {
    return values[c * 4 + r];
  }

  /**
   * Sets the matrix value at the given row and column.
   * @param r  Row index, valid range 0..3.
   * @param c  Column index, valid range 0..3.
   */
  void setRowColumn(int r, int c, float value) {
    values[c * 4 + r] = value;
  }

  /**
   * Returns a reference to a constant identity Matrix3D. The returned Matrix3D is:
   *
   *       | 1 0 0 0 |
   *       | 0 1 0 0 |
   *       | 0 0 1 0 |
   *       | 0 0 0 1 |
   */
  static const Matrix3D& I();

  /**
   * Creates a Matrix3D that scales by (sx, sy, sz). The returned matrix is:
   *
   *       | sx  0  0  0 |
   *       |  0 sy  0  0 |
   *       |  0  0 sz  0 |
   *       |  0  0  0  1 |
   */
  static Matrix3D MakeScale(float sx, float sy, float sz) {
    return {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1};
  }

  /**
   * Creates a Matrix3D that rotates by the given angle (in degrees) around the specified axis.
   * The returned matrix is:
   *
   *   | t*x*x + c   t*x*y + s*z   t*x*z - s*y   0 |
   *   | t*x*y - s*z t*y*y + c     t*y*z + s*x   0 |
   *   | t*x*z + s*y t*y*z - s*x   t*z*z + c     0 |
   *   |     0           0             0         1 |
   *
   * where:
   *   x, y, z = normalized components of axis
   *   c = cos(degrees)
   *   s = sin(degrees)
   *   t = 1 - c
   * @param axis The axis to rotate about.
   * @param degrees The angle of rotation in degrees.
   */
  static Matrix3D MakeRotate(const Vec3& axis, float degrees) {
    Matrix3D m;
    m.setRotate(axis, degrees);
    return m;
  }

  /**
   * Creates a Matrix3D that translates by (tx, ty, tz). The returned matrix is:
   *
   *      | 1 0 0 tx |
   *      | 0 1 0 ty |
   *      | 0 0 1 tz |
   *      | 0 0 0 1  |
   */
  static Matrix3D MakeTranslate(float tx, float ty, float tz) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1};
  }

  /**
   * Pre-concatenates a rotation to this matrix. M' = R * M.
   */
  void preRotate(const Vec3& axis, float degrees);

  /**
  * Post-concatenates a rotation to this matrix. M' = M * R.
  */
  void postRotate(const Vec3& axis, float degrees);

  /**
   * Post-concatenates a translation to this matrix. M' = M * T.
   */
  void postTranslate(float tx, float ty, float tz);

  /**
   * Calculates the inverse of the current matrix and stores the result in the Matrix3D object
   * pointed to by inverse.
   * @param inverse Pointer to the Matrix3D object used to store the inverse matrix. Must not be
   * nullptr. If the current matrix is not invertible, inverse will not be modified.
   * @return Returns true if the inverse matrix exists; otherwise, returns false.
   */
  bool invert(Matrix3D* inverse = nullptr) const;

  /**
   * Creates a view matrix for a camera. This is commonly used to transform world coordinates to
   * camera (view) coordinates in 3D graphics.
   * @param eye The position of the camera.
   * @param center The point the camera is looking at.
   * @param up The up direction for the camera.
   */
  static Matrix3D LookAt(const Vec3& eye, const Vec3& center, const Vec3& up);

  /**
   * Creates a standard perspective projection matrix. This matrix maps 3D coordinates into
   * clip coordinates for perspective rendering.
   * The standard projection model is established by defining the camera position, orientation,
   * field of view, and near/far planes. Points inside the view frustum are projected onto the near
   * plane.
   * @param fovyDegrees Field of view angle in degrees (vertical).
   * @param aspect Aspect ratio (width / height).
   * @param nearZ Distance to the near clipping plane.
   * @param farZ Distance to the far clipping plane.
   */
  static Matrix3D Perspective(float fovyDegrees, float aspect, float nearZ, float farZ);

  /**
   * Maps a rectangle using this matrix.
   * If the matrix contains a perspective transformation, each corner of the rectangle is mapped as a
   * 4D point (x, y, 0, 1), and the resulting rectangle is computed from the projected points
   * (after perspective division).
   */
  Rect mapRect(const Rect& src) const;

  /**
   * Returns true if the matrix is an identity matrix.
   */
  bool isIdentity() const {
    return *this == I();
  }

  bool operator==(const Matrix3D& other) const;

  bool operator!=(const Matrix3D& other) const {
    return !(other == *this);
  }

  friend Matrix3D operator*(const Matrix3D& a, const Matrix3D& b) {
    Matrix3D result;
    result.setConcat(a, b);
    return result;
  }

 private:
  constexpr Matrix3D(float m00, float m01, float m02, float m03, float m10, float m11, float m12,
                     float m13, float m20, float m21, float m22, float m23, float m30, float m31,
                     float m32, float m33)
      : values{m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33} {
  }

  /**
   * Copies the matrix values into a 16-element array in column-major order.
   */
  void getColMajor(float buffer[16]) const {
    memcpy(buffer, values, sizeof(values));
  }

  /**
   * Copies the matrix values into a 16-element array in row-major order.
   */
  void getRowMajor(float buffer[16]) const;

  /**
   * Concatenates two matrices and stores the result in this matrix. M' = a * b.
   */
  void setConcat(const Matrix3D& a, const Matrix3D& b);

  /**
   * Concatenates the given matrix with this matrix, and stores the result in this matrix. M' = m * M.
   */
  void preConcat(const Matrix3D& m);

  /**
   * Concatenates this matrix with the given matrix, and stores the result in this matrix. M' = M * m.
   */
  void postConcat(const Matrix3D& m);

  /**
   * Pre-concatenates a scale to this matrix. M' = S * M.
   */
  void preScale(float sx, float sy, float sz);

  /**
   * Post-concatenates a scale to this matrix. M' = M * S.
   */
  void postScale(float sx, float sy, float sz);

  /**
   * Pre-concatenates a translation to this matrix. M' = T * M.
   */
  void preTranslate(float tx, float ty, float tz);

  /**
   * Returns the transpose of the current matrix.
   */
  Matrix3D transpose() const;

  /**
   * Maps a 4D point (x, y, z, w) using this matrix.
   * If the current matrix contains a perspective transformation, the returned Vec4 is not
   * perspective-divided; i.e., the w component of the result may not be 1.
   */
  Vec4 mapPoint(float x, float y, float z, float w) const;

  Vec4 getCol(int i) const {
    Vec4 v;
    memcpy(&v, values + i * 4, sizeof(Vec4));
    return v;
  }

  void setAll(float m00, float m01, float m02, float m03, float m10, float m11, float m12,
              float m13, float m20, float m21, float m22, float m23, float m30, float m31,
              float m32, float m33);

  void setRow(int i, const Vec4& v) {
    values[i + 0] = v.x;
    values[i + 4] = v.y;
    values[i + 8] = v.z;
    values[i + 12] = v.w;
  }

  void setColumn(int i, const Vec4& v) {
    memcpy(&values[i * 4], v.ptr(), sizeof(v));
  }

  void setIdentity() {
    *this = Matrix3D();
  }

  void setRotate(const Vec3& axis, float degrees);

  void setRotateUnit(const Vec3& axis, float degrees);

  void setRotateUnitSinCos(const Vec3& axis, float sinV, float cosV);

  bool hasPerspective() const {
    return (values[3] != 0 || values[7] != 0 || values[11] != 0 || values[15] != 1);
  }

  Vec4 operator*(const Vec4& v) const {
    return this->mapPoint(v.x, v.y, v.z, v.w);
  }

  float values[16] = {.0f};
};

}  // namespace tgfx
