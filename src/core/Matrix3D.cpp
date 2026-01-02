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

#include "tgfx/core/Matrix3D.h"
#include "utils/Log.h"
#include "utils/MathExtra.h"

namespace tgfx {

// A 4x4 matrix's first three rows describe a 3D affine transformation, which can also be understood
// as transforming the current coordinate system to a new coordinate system. The first three columns
// of this matrix represent the coordinates of the new coordinate system's basis vectors in the old
// coordinate system, while the 4th column describes the position of the new coordinate system's
// origin in the old coordinate system. The 4th row of the matrix describes projection coefficients.
// For column-major stored matrices, the meaning of matrix elements and their corresponding index
// definitions are as follows. Following the general rules in image processing, matrix element
// m[i][j] represents the element at row i+1, column j+1, and this definition is maintained when
// naming matrix elements, for example, SKEW_Y_X represents the relationship between X and Y, not Y
// and X.
//   | SCALE_X      SKEW_X_Y    SKEW_X_Z    TRANS_X    |
//   | SKEW_Y_X     SCALE_Y     SKEW_Y_Z    TRANS_Y    |
//   | SKEW_Z_X     SKEW_Z_Y    SCALE_Z     TRANS_Z    |
//   | PERS_X       PERS_Y      PERS_Z      PERS_SCALE |
// Skew value of new coordinate system's X-axis relative to old coordinate system's Y-axis
static constexpr int SKEW_Y_X = 1;
// Skew value of new coordinate system's X-axis relative to old coordinate system's Z-axis
static constexpr int SKEW_Z_X = 2;

// Skew value of new coordinate system's Y-axis relative to old coordinate system's X-axis
static constexpr int SKEW_X_Y = 4;
// Skew value of new coordinate system's Y-axis relative to old coordinate system's Z-axis
static constexpr int SKEW_Z_Y = 6;

// Skew value of new coordinate system's Z-axis relative to old coordinate system's X-axis
static constexpr int SKEW_X_Z = 8;
// Skew value of new coordinate system's Z-axis relative to old coordinate system's Y-axis
static constexpr int SKEW_Y_Z = 9;

// X-coordinate of new coordinate system's origin in old coordinate system
static constexpr int TRANS_X = 12;
// Y-coordinate of new coordinate system's origin in old coordinate system
static constexpr int TRANS_Y = 13;

static void TransposeArrays(const float src[16], float dst[16]) {
  dst[0] = src[0];
  dst[1] = src[4];
  dst[2] = src[8];
  dst[3] = src[12];
  dst[4] = src[1];
  dst[5] = src[5];
  dst[6] = src[9];
  dst[7] = src[13];
  dst[8] = src[2];
  dst[9] = src[6];
  dst[10] = src[10];
  dst[11] = src[14];
  dst[12] = src[3];
  dst[13] = src[7];
  dst[14] = src[11];
  dst[15] = src[15];
}

static bool InvertMatrix3D(const float inMat[16], float outMat[16]) {
  // a[ij] represents the element at column i, row j
  const float a00 = inMat[0];
  const float a01 = inMat[1];
  const float a02 = inMat[2];
  const float a03 = inMat[3];
  const float a10 = inMat[4];
  const float a11 = inMat[5];
  const float a12 = inMat[6];
  const float a13 = inMat[7];
  const float a20 = inMat[8];
  const float a21 = inMat[9];
  const float a22 = inMat[10];
  const float a23 = inMat[11];
  const float a30 = inMat[12];
  const float a31 = inMat[13];
  const float a32 = inMat[14];
  const float a33 = inMat[15];

  // Precompute all possible 2x2 determinants to optimize the calculation of cofactors, where a
  // cofactor refers to the determinant of the remaining matrix after removing a specific row and
  // column.
  float b00 = a00 * a11 - a01 * a10;
  float b01 = a00 * a12 - a02 * a10;
  float b02 = a00 * a13 - a03 * a10;
  float b03 = a01 * a12 - a02 * a11;
  float b04 = a01 * a13 - a03 * a11;
  float b05 = a02 * a13 - a03 * a12;
  float b06 = a20 * a31 - a21 * a30;
  float b07 = a20 * a32 - a22 * a30;
  float b08 = a20 * a33 - a23 * a30;
  float b09 = a21 * a32 - a22 * a31;
  float b10 = a21 * a33 - a23 * a31;
  float b11 = a22 * a33 - a23 * a32;

  const float determinant = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
  if (FloatNearlyZero(determinant, FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO)) {
    return false;
  }
  const float invdet = 1.f / determinant;
  b00 *= invdet;
  b01 *= invdet;
  b02 *= invdet;
  b03 *= invdet;
  b04 *= invdet;
  b05 *= invdet;
  b06 *= invdet;
  b07 *= invdet;
  b08 *= invdet;
  b09 *= invdet;
  b10 *= invdet;
  b11 *= invdet;

  outMat[0] = a11 * b11 - a12 * b10 + a13 * b09;
  outMat[1] = a02 * b10 - a01 * b11 - a03 * b09;
  outMat[2] = a31 * b05 - a32 * b04 + a33 * b03;
  outMat[3] = a22 * b04 - a21 * b05 - a23 * b03;
  outMat[4] = a12 * b08 - a10 * b11 - a13 * b07;
  outMat[5] = a00 * b11 - a02 * b08 + a03 * b07;
  outMat[6] = a32 * b02 - a30 * b05 - a33 * b01;
  outMat[7] = a20 * b05 - a22 * b02 + a23 * b01;
  outMat[8] = a10 * b10 - a11 * b08 + a13 * b06;
  outMat[9] = a01 * b08 - a00 * b10 - a03 * b06;
  outMat[10] = a30 * b04 - a31 * b02 + a33 * b00;
  outMat[11] = a21 * b02 - a20 * b04 - a23 * b00;
  outMat[12] = a11 * b07 - a10 * b09 - a12 * b06;
  outMat[13] = a00 * b09 - a01 * b07 + a02 * b06;
  outMat[14] = a31 * b01 - a30 * b03 - a32 * b00;
  outMat[15] = a20 * b03 - a21 * b01 + a22 * b00;

  if (!FloatsAreFinite(outMat, 16)) {
    return false;
  }

  return true;
}

static Rect MapRectAffine(const Rect& srcRect, const float mat[16]) {
  constexpr Vec4 flip{1.f, 1.f, -1.f, -1.f};

  auto c0 = Shuffle<0, 1, 0, 1>(Vec2::Load(mat)) * flip;
  auto c1 = Shuffle<0, 1, 0, 1>(Vec2::Load(mat + 4)) * flip;
  auto c3 = Shuffle<0, 1, 0, 1>(Vec2::Load(mat + 12));

  auto p0 = Min(c0 * srcRect.left + c1 * srcRect.top, c0 * srcRect.right + c1 * srcRect.top);
  auto p1 = Min(c0 * srcRect.left + c1 * srcRect.bottom, c0 * srcRect.right + c1 * srcRect.bottom);
  auto minMax = c3 + flip * Min(p0, p1);

  return {minMax.x, minMax.y, minMax.z, minMax.w};
}

static Rect MapRectPerspective(const Rect& srcRect, const float mat[16]) {
  auto c0 = Vec4::Load(mat);
  auto c1 = Vec4::Load(mat + 4);
  auto c3 = Vec4::Load(mat + 12);

  auto tl = c0 * srcRect.left + c1 * srcRect.top + c3;
  auto tr = c0 * srcRect.right + c1 * srcRect.top + c3;
  auto bl = c0 * srcRect.left + c1 * srcRect.bottom + c3;
  auto br = c0 * srcRect.right + c1 * srcRect.bottom + c3;

  constexpr Vec4 flip{1.f, 1.f, -1.f, -1.f};
  auto project = [&flip](const Vec4& p0, const Vec4& p1, const Vec4& p2) {
    const float w0 = p0[3];
    if (constexpr float w0PlaneDistance = 1.f / (1 << 14); w0 >= w0PlaneDistance) {
      return flip * Shuffle<0, 1, 0, 1>(Vec2(p0.x, p0.y)) / w0;
    } else {
      auto clip = [&](const Vec4& p) {
        if (const float w = p[3]; w >= w0PlaneDistance) {
          const float t = (w0PlaneDistance - w0) / (w - w0);
          auto c = (t * Vec2::Load(p.ptr()) + (1.f - t) * Vec2::Load(p0.ptr())) / w0PlaneDistance;
          return flip * Shuffle<0, 1, 0, 1>(c);
        } else {
          return Vec4(std::numeric_limits<float>::infinity());
        }
      };
      return Min(clip(p1), clip(p2));
    }
  };

  auto p0 = Min(project(tl, tr, bl), project(tr, br, tl));
  auto p1 = Min(project(br, bl, tr), project(bl, tl, br));
  auto minMax = flip * Min(p0, p1);
  return {minMax.x, minMax.y, minMax.z, minMax.w};
}

Vec4 Matrix3D::getRow(int i) const {
  DEBUG_ASSERT(i >= 0 && i < 4);
  return {values[i], values[i + 4], values[i + 8], values[i + 12]};
}

void Matrix3D::setRow(int i, const Vec4& v) {
  DEBUG_ASSERT(i >= 0 && i < 4);
  values[i] = v.x;
  values[i + 4] = v.y;
  values[i + 8] = v.z;
  values[i + 12] = v.w;
}

float Matrix3D::getTranslateX() const {
  return values[TRANS_X];
}

float Matrix3D::getTranslateY() const {
  return values[TRANS_Y];
}

const Matrix3D& Matrix3D::I() {
  static constexpr Matrix3D identity;
  return identity;
}

void Matrix3D::postScale(float sx, float sy, float sz) {
  if (sx == 1 && sy == 1 && sz == 1) {
    return;
  }
  auto m = MakeScale(sx, sy, sz);
  this->postConcat(m);
}

void Matrix3D::preRotate(const Vec3& axis, float degrees) {
  auto m = MakeRotate(axis, degrees);
  preConcat(m);
}

void Matrix3D::postRotate(const Vec3& axis, float degrees) {
  auto m = MakeRotate(axis, degrees);
  postConcat(m);
}

void Matrix3D::preTranslate(float tx, float ty, float tz) {
  auto c0 = getCol(0);
  auto c1 = getCol(1);
  auto c2 = getCol(2);
  auto c3 = getCol(3);

  setColumn(3, (c0 * tx + c1 * ty + c2 * tz + c3));
}

void Matrix3D::postTranslate(float tx, float ty, float tz) {
  Vec4 t = {tx, ty, tz, 0};
  setColumn(0, getCol(0) + t * values[3]);
  setColumn(1, getCol(1) + t * values[7]);
  setColumn(2, getCol(2) + t * values[11]);
  setColumn(3, getCol(3) + t * values[15]);
}

void Matrix3D::postSkew(float kxy, float kxz, float kyx, float kyz, float kzx, float kzy) {
  Matrix3D m;
  m.setSkew(kxy, kxz, kyx, kyz, kzx, kzy);
  postConcat(m);
}

void Matrix3D::preConcat(const Matrix3D& m) {
  setConcat(*this, m);
}

void Matrix3D::postConcat(const Matrix3D& m) {
  setConcat(m, *this);
}

bool Matrix3D::invert(Matrix3D* inverse) const {
  float result[16];
  if (!InvertMatrix3D(values, result)) {
    return false;
  }
  if (inverse != nullptr) {
    memcpy(inverse->values, result, sizeof(result));
  }
  return true;
}

Matrix3D Matrix3D::LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
  auto viewZ = Vec3::Normalize(eye - center);
  auto viewX = Vec3::Normalize(up.cross(viewZ));
  auto viewY = viewZ.cross(viewX);
  const Matrix3D m(viewX.x, viewY.x, viewZ.x, 0.f, viewX.y, viewY.y, viewZ.y, 0.f, viewX.z, viewY.z,
                   viewZ.z, 0.f, -viewX.dot(eye), -viewY.dot(eye), -viewZ.dot(eye), 1.f);
  return m;
}

Matrix3D Matrix3D::Perspective(float fovyDegress, float aspect, float nearZ, float farZ) {
  auto fovyRadians = DegreesToRadians(fovyDegress);
  const float cotan = 1.f / tanf(fovyRadians / 2.f);

  const Matrix3D m(cotan / aspect, 0.f, 0.f, 0.f, 0.f, cotan, 0.f, 0.f, 0.f, 0.f,
                   (nearZ + farZ) / (nearZ - farZ), -1.f, 0.f, 0.f,
                   (2.f * nearZ * farZ) / (nearZ - farZ), 0.f);
  return m;
}

Rect Matrix3D::mapRect(const Rect& src) const {
  if (hasPerspective()) {
    return MapRectPerspective(src, values);
  } else {
    return MapRectAffine(src, values);
  }
}

void Matrix3D::mapRect(Rect* rect) const {
  if (rect == nullptr) {
    return;
  }
  *rect = mapRect(*rect);
}

Vec3 Matrix3D::mapPoint(const Vec3& point) const {
  auto r = mapHomogeneous(point.x, point.y, point.z, 1.f);
  return {IEEEFloatDivide(r.x, r.w), IEEEFloatDivide(r.y, r.w), IEEEFloatDivide(r.z, r.w)};
}

Vec3 Matrix3D::mapVector(const Vec3& vector) const {
  auto r = mapHomogeneous(vector.x, vector.y, vector.z, 0.f);
  return {r.x, r.y, r.z};
}

Vec4 Matrix3D::mapHomogeneous(float x, float y, float z, float w) const {
  auto c0 = getCol(0);
  auto c1 = getCol(1);
  auto c2 = getCol(2);
  auto c3 = getCol(3);

  const Vec4 result = (c0 * x + c1 * y + c2 * z + c3 * w);
  return result;
}

bool Matrix3D::operator==(const Matrix3D& other) const {
  if (this == &other) {
    return true;
  }

  auto a0 = getCol(0);
  auto a1 = getCol(1);
  auto a2 = getCol(2);
  auto a3 = getCol(3);

  auto b0 = other.getCol(0);
  auto b1 = other.getCol(1);
  auto b2 = other.getCol(2);
  auto b3 = other.getCol(3);

  return ((a0 == b0) && (a1 == b1) && (a2 == b2) && (a3 == b3));
}

void Matrix3D::getRowMajor(float buffer[16]) const {
  TransposeArrays(values, buffer);
}

void Matrix3D::setConcat(const Matrix3D& a, const Matrix3D& b) {
  auto c0 = a.getCol(0);
  auto c1 = a.getCol(1);
  auto c2 = a.getCol(2);
  auto c3 = a.getCol(3);

  auto compute = [&](Vec4 v) { return c0 * v[0] + c1 * v[1] + c2 * v[2] + c3 * v[3]; };

  auto m0 = compute(b.getCol(0));
  auto m1 = compute(b.getCol(1));
  auto m2 = compute(b.getCol(2));
  auto m3 = compute(b.getCol(3));

  setColumn(0, m0);
  setColumn(1, m1);
  setColumn(2, m2);
  setColumn(3, m3);
}

void Matrix3D::preScale(float sx, float sy, float sz) {
  if (sx == 1 && sy == 1 && sz == 1) {
    return;
  }

  auto c0 = getCol(0);
  auto c1 = getCol(1);
  auto c2 = getCol(2);

  setColumn(0, c0 * sx);
  setColumn(1, c1 * sy);
  setColumn(2, c2 * sz);
}

Matrix3D Matrix3D::transpose() const {
  Matrix3D m;
  TransposeArrays(values, m.values);
  return m;
}

void Matrix3D::setAll(float m00, float m01, float m02, float m03, float m10, float m11, float m12,
                      float m13, float m20, float m21, float m22, float m23, float m30, float m31,
                      float m32, float m33) {
  values[0] = m00;
  values[1] = m01;
  values[2] = m02;
  values[3] = m03;
  values[4] = m10;
  values[5] = m11;
  values[6] = m12;
  values[7] = m13;
  values[8] = m20;
  values[9] = m21;
  values[10] = m22;
  values[11] = m23;
  values[12] = m30;
  values[13] = m31;
  values[14] = m32;
  values[15] = m33;
}

void Matrix3D::setRotate(const Vec3& axis, float degrees) {
  if (auto len = axis.length(); len > 0 && (len * 0 == 0)) {
    setRotateUnit(axis * (1.f / len), degrees);
  } else {
    setIdentity();
  }
}

void Matrix3D::setRotateUnit(const Vec3& axis, float degrees) {
  auto radians = DegreesToRadians(degrees);
  setRotateUnitSinCos(axis, sin(radians), cos(radians));
}

void Matrix3D::setRotateUnitSinCos(const Vec3& axis, float sinAngle, float cosAngle) {
  const float x = axis.x;
  const float y = axis.y;
  const float z = axis.z;
  const float c = cosAngle;
  const float s = sinAngle;
  const float t = 1 - c;

  setAll(t * x * x + c, t * x * y + s * z, t * x * z - s * y, 0, t * x * y - s * z, t * y * y + c,
         t * y * z + s * x, 0, t * x * z + s * y, t * y * z - s * x, t * z * z + c, 0, 0, 0, 0, 1);
}

void Matrix3D::setSkew(float kxy, float kxz, float kyx, float kyz, float kzx, float kzy) {
  values[SKEW_X_Y] = kxy;
  values[SKEW_X_Z] = kxz;
  values[SKEW_Y_X] = kyx;
  values[SKEW_Y_Z] = kyz;
  values[SKEW_Z_X] = kzx;
  values[SKEW_Z_Y] = kzy;
}

}  // namespace tgfx
