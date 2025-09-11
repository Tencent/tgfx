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
#include "MathVector.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class Matrix3D {
 public:
  constexpr Matrix3D() : Matrix3D(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) {
  }

  static Matrix3D MakeRowMajor(const float r[16]) {
    return {r[0], r[4], r[8],  r[12], r[1], r[5], r[9],  r[13],
            r[2], r[6], r[10], r[14], r[3], r[7], r[11], r[15]};
  }

  static Matrix3D MakeColMajor(const float c[16]) {
    return {c[0], c[1], c[2],  c[3],  c[4],  c[5],  c[6],  c[7],
            c[8], c[9], c[10], c[11], c[12], c[13], c[14], c[15]};
  }

  static Matrix3D MakeCols(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3) {
    return {c0.x, c0.y, c0.z, c0.w, c1.x, c1.y, c1.z, c1.w,
            c2.x, c2.y, c2.z, c2.w, c3.x, c3.y, c3.z, c3.w};
  }

  static const Matrix3D& I();

  static Matrix3D MakeScale(float sx, float sy, float sz) {
    return {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1};
  }

  static Matrix3D MakeTranslate(float tx, float ty, float tz) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1};
  }

  static Matrix3D MakeRotate(const Vec3& axis, float degrees) {
    Matrix3D m;
    m.setRotate(axis, degrees);
    return m;
  }

  void getColMajor(float buffer[16]) const {
    memcpy(buffer, values, sizeof(values));
  }

  void getRowMajor(float buffer[16]) const;

  void setConcat(const Matrix3D& a, const Matrix3D& b);

  void preConcat(const Matrix3D& m);

  void postConcat(const Matrix3D& m);

  void preScale(float sx, float sy, float sz);

  void postScale(float sx, float sy, float sz);

  void preTranslate(float tx, float ty, float tz);

  void postTranslate(float tx, float ty, float tz);

  void preRotate(const Vec3& axis, float degrees);

  void postRotate(const Vec3& axis, float degrees);

  bool invert(Matrix3D* inverse) const;

  Matrix3D transpose() const;

  Vec4 mapPoint(float x, float y, float z, float w) const;

  Rect mapRect(const Rect& src) const;

  static Matrix3D LookAt(const Vec3& eye, const Vec3& center, const Vec3& up);

  static Matrix3D Perspective(float fovyDegress, float aspect, float nearZ, float farZ);

 private:
  constexpr Matrix3D(float m00, float m01, float m02, float m03, float m10, float m11, float m12,
                     float m13, float m20, float m21, float m22, float m23, float m30, float m31,
                     float m32, float m33)
      : values{m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33} {
  }

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

  void setCol(int i, const Vec4& v) {
    memcpy(&values[i * 4], v.ptr(), sizeof(v));
  }

  void setRowCol(int r, int c, float value) {
    values[c * 4 + r] = value;
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

  bool operator==(const Matrix3D& other) const;

  bool operator!=(const Matrix3D& other) const {
    return !(other == *this);
  }

  friend Matrix3D operator*(const Matrix3D& a, const Matrix3D& b) {
    Matrix3D result;
    result.setConcat(a, b);
    return result;
  }

  Vec4 operator*(const Vec4& v) const {
    return this->mapPoint(v.x, v.y, v.z, v.w);
  }

  float values[16];
};

}  // namespace tgfx