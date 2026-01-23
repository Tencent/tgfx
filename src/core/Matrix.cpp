/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Matrix.h"
#include <cfloat>
#include "core/utils/MathExtra.h"

namespace tgfx {

void Matrix::reset() {
  *this = Matrix();
}

bool operator==(const Matrix& a, const Matrix& b) {
  const float* ma = a.values;
  const float* mb = b.values;
  return ma[0] == mb[0] && ma[1] == mb[1] && ma[2] == mb[2] && ma[3] == mb[3] && ma[4] == mb[4] &&
         ma[5] == mb[5] && ma[6] == mb[6] && ma[7] == mb[7] && ma[8] == mb[8];
}

Matrix operator*(const Matrix& a, const Matrix& b) {
  Matrix result;
  result.setConcat(a, b);
  return result;
}

void Matrix::setAll(float sx, float kx, float tx, float ky, float sy, float ty) {
  values[SCALE_X] = sx;
  values[SKEW_X] = kx;
  values[TRANS_X] = tx;
  values[SKEW_Y] = ky;
  values[SCALE_Y] = sy;
  values[TRANS_Y] = ty;
  values[PERSP_0] = 0;
  values[PERSP_1] = 0;
  values[PERSP_2] = 1;
  this->setTypeMask(UnknownMask);
}

void Matrix::setAll(float sx, float kx, float tx, float ky, float sy, float ty, float p0, float p1,
                    float p2) {
  values[SCALE_X] = sx;
  values[SKEW_X] = kx;
  values[TRANS_X] = tx;
  values[SKEW_Y] = ky;
  values[SCALE_Y] = sy;
  values[TRANS_Y] = ty;
  values[PERSP_0] = p0;
  values[PERSP_1] = p1;
  values[PERSP_2] = p2;
  this->setTypeMask(UnknownMask);
}

void Matrix::setTranslate(float tx, float ty) {
  if ((tx != 0) | (ty != 0)) {
    values[TRANS_X] = tx;
    values[TRANS_Y] = ty;

    values[SCALE_X] = values[SCALE_Y] = 1;
    values[SKEW_X] = values[SKEW_Y] = 0;
    values[PERSP_0] = values[PERSP_1] = 0;
    values[PERSP_2] = 1;
    this->setTypeMask(TranslateMask | RectStayRectMask);
  } else {
    this->reset();
  }
}

inline float sdot(float a, float b, float c, float d) {
  return a * b + c * d;
}

void Matrix::preTranslate(float tx, float ty) {
  const unsigned mask = this->getType();
  // Check perspective first, as it requires full matrix multiplication
  if (mask & PerspectiveMask) {
    Matrix m;
    m.setTranslate(tx, ty);
    this->preConcat(m);
    return;
  }

  if (mask <= TranslateMask) {
    values[TRANS_X] += tx;
    values[TRANS_Y] += ty;
  } else {
    values[TRANS_X] += sdot(values[SCALE_X], tx, values[SKEW_X], ty);
    values[TRANS_Y] += sdot(values[SKEW_Y], tx, values[SCALE_Y], ty);
  }
  this->updateTranslateMask();
}

void Matrix::postTranslate(float tx, float ty) {
  if (this->getType() & PerspectiveMask) {
    Matrix m;
    m.setTranslate(tx, ty);
    this->postConcat(m);
  } else {
    values[TRANS_X] += tx;
    values[TRANS_Y] += ty;
    this->updateTranslateMask();
  }
}

void Matrix::setScale(float sx, float sy, float px, float py) {
  if (1 == sx && 1 == sy) {
    this->reset();
  } else {
    this->setScaleTranslate(sx, sy, px - sx * px, py - sy * py);
  }
}

void Matrix::setScale(float sx, float sy) {
  if (1 == sx && 1 == sy) {
    this->reset();
  } else {
    auto rectMask = (sx != 0 && sy != 0) ? RectStayRectMask : 0;
    values[SCALE_X] = sx;
    values[SCALE_Y] = sy;
    values[TRANS_X] = values[TRANS_Y] = values[SKEW_X] = values[SKEW_Y] = 0;
    values[PERSP_0] = values[PERSP_1] = 0;
    values[PERSP_2] = 1;
    this->setTypeMask(ScaleMask | rectMask);
  }
}

void Matrix::preScale(float sx, float sy, float px, float py) {
  if (1 == sx && 1 == sy) {
    return;
  }
  Matrix m;
  m.setScale(sx, sy, px, py);
  this->preConcat(m);
}

void Matrix::preScale(float sx, float sy) {
  if (1 == sx && 1 == sy) {
    return;
  }
  values[SCALE_X] *= sx;
  values[SKEW_Y] *= sx;
  values[PERSP_0] *= sx;

  values[SKEW_X] *= sy;
  values[SCALE_Y] *= sy;
  values[PERSP_1] *= sy;
  if (values[SCALE_X] == 1 && values[SCALE_Y] == 1 &&
      !(typeMask & (PerspectiveMask | AffineMask))) {
    this->clearTypeMask(ScaleMask);
  } else {
    this->orTypeMask(ScaleMask);
    if (sx == 0 || sy == 0) {
      this->clearTypeMask(RectStayRectMask);
    }
  }
}

void Matrix::postScale(float sx, float sy, float px, float py) {
  if (1 == sx && 1 == sy) {
    return;
  }
  Matrix m;
  m.setScale(sx, sy, px, py);
  this->postConcat(m);
}

void Matrix::postScale(float sx, float sy) {
  if (1 == sx && 1 == sy) {
    return;
  }
  Matrix m;
  m.setScale(sx, sy);
  this->postConcat(m);
}

void Matrix::setSinCos(float sinV, float cosV, float px, float py) {
  const float oneMinusCosV = 1 - cosV;
  values[SCALE_X] = cosV;
  values[SKEW_X] = -sinV;
  values[TRANS_X] = sdot(sinV, py, oneMinusCosV, px);
  values[SKEW_Y] = sinV;
  values[SCALE_Y] = cosV;
  values[TRANS_Y] = sdot(-sinV, px, oneMinusCosV, py);
  values[PERSP_0] = values[PERSP_1] = 0;
  values[PERSP_2] = 1;
  this->setTypeMask(UnknownMask);
}

void Matrix::setSinCos(float sinV, float cosV) {
  values[SCALE_X] = cosV;
  values[SKEW_X] = -sinV;
  values[TRANS_X] = 0;
  values[SKEW_Y] = sinV;
  values[SCALE_Y] = cosV;
  values[TRANS_Y] = 0;
  values[PERSP_0] = values[PERSP_1] = 0;
  values[PERSP_2] = 1;
  this->setTypeMask(UnknownMask);
}

void Matrix::setRotate(float degrees, float px, float py) {
  float rad = DegreesToRadians(degrees);
  this->setSinCos(SinSnapToZero(rad), CosSnapToZero(rad), px, py);
}

void Matrix::setRotate(float degrees) {
  float rad = DegreesToRadians(degrees);
  this->setSinCos(SinSnapToZero(rad), CosSnapToZero(rad));
}

void Matrix::preRotate(float degrees, float px, float py) {
  Matrix m;
  m.setRotate(degrees, px, py);
  this->preConcat(m);
}

void Matrix::preRotate(float degrees) {
  Matrix m;
  m.setRotate(degrees);
  this->preConcat(m);
}

void Matrix::postRotate(float degrees, float px, float py) {
  Matrix m;
  m.setRotate(degrees, px, py);
  this->postConcat(m);
}

void Matrix::postRotate(float degrees) {
  Matrix m;
  m.setRotate(degrees);
  this->postConcat(m);
}

void Matrix::setSkew(float kx, float ky, float px, float py) {
  values[SCALE_X] = 1;
  values[SKEW_X] = kx;
  values[TRANS_X] = -kx * py;
  values[SKEW_Y] = ky;
  values[SCALE_Y] = 1;
  values[TRANS_Y] = -ky * px;
  values[PERSP_0] = values[PERSP_1] = 0;
  values[PERSP_2] = 1;
  this->setTypeMask(UnknownMask);
}

void Matrix::setSkew(float kx, float ky) {
  values[SCALE_X] = 1;
  values[SKEW_X] = kx;
  values[TRANS_X] = 0;
  values[SKEW_Y] = ky;
  values[SCALE_Y] = 1;
  values[TRANS_Y] = 0;
  values[PERSP_0] = values[PERSP_1] = 0;
  values[PERSP_2] = 1;
  this->setTypeMask(UnknownMask);
}

void Matrix::preSkew(float kx, float ky, float px, float py) {
  Matrix m;
  m.setSkew(kx, ky, px, py);
  this->preConcat(m);
}

void Matrix::preSkew(float kx, float ky) {
  Matrix m;
  m.setSkew(kx, ky);
  this->preConcat(m);
}

void Matrix::postSkew(float kx, float ky, float px, float py) {
  Matrix m;
  m.setSkew(kx, ky, px, py);
  this->postConcat(m);
}

void Matrix::postSkew(float kx, float ky) {
  Matrix m;
  m.setSkew(kx, ky);
  this->postConcat(m);
}

static bool OnlyScaleAndTranslate(unsigned mask) {
  return 0 == (mask & (Matrix::AffineMask | Matrix::PerspectiveMask));
}

void Matrix::setConcat(const Matrix& first, const Matrix& second) {
  if (first.isTriviallyIdentity()) {
    *this = second;
    return;
  }
  if (second.isTriviallyIdentity()) {
    *this = first;
    return;
  }

  // If either matrix has perspective, do full 3x3 multiply
  if (first.hasPerspective() || second.hasPerspective()) {
    ConcatMatrix(first, second, *this);
    return;
  }

  auto& matrixA = first.values;
  auto& matrixB = second.values;
  TypeMask aType = first.getType();
  TypeMask bType = second.getType();
  // No perspective, use optimized affine path
  auto sx = matrixB[SCALE_X] * matrixA[SCALE_X];
  auto kx = 0.0f;
  auto ky = 0.0f;
  auto sy = matrixB[SCALE_Y] * matrixA[SCALE_Y];
  auto tx = matrixB[TRANS_X] * matrixA[SCALE_X] + matrixA[TRANS_X];
  auto ty = matrixB[TRANS_Y] * matrixA[SCALE_Y] + matrixA[TRANS_Y];
  if (OnlyScaleAndTranslate(aType | bType)) {
    this->setScaleTranslate(sx, sy, tx, ty);
  } else {
    sx += matrixB[SKEW_Y] * matrixA[SKEW_X];
    sy += matrixB[SKEW_X] * matrixA[SKEW_Y];
    ky += matrixB[SCALE_X] * matrixA[SKEW_Y] + matrixB[SKEW_Y] * matrixA[SCALE_Y];
    kx += matrixB[SKEW_X] * matrixA[SCALE_X] + matrixB[SCALE_Y] * matrixA[SKEW_X];
    tx += matrixB[TRANS_Y] * matrixA[SKEW_X];
    ty += matrixB[TRANS_X] * matrixA[SKEW_Y];
    setAll(sx, kx, tx, ky, sy, ty);
  }
}

void Matrix::preConcat(const Matrix& matrix) {
  // check for identity first, so we don't do a needless copy of ourselves to ourselves inside
  // setConcat()
  if (!matrix.isIdentity()) {
    this->setConcat(*this, matrix);
  }
}

void Matrix::postConcat(const Matrix& matrix) {
  // check for identity first, so we don't do a needless copy of ourselves to ourselves inside
  // setConcat()
  if (!matrix.isIdentity()) {
    this->setConcat(matrix, *this);
  }
}

bool Matrix::invertible() const {
  float determinant = values[SCALE_X] * values[SCALE_Y] - values[SKEW_Y] * values[SKEW_X];
  return !(FloatNearlyZero(determinant, FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO));
}

enum { TranslateShift, ScaleShift, AffineShift, RectStaysRectShift = 4 };

static const int32_t Scalar1Int = 0x3f800000;

uint8_t Matrix::computeTypeMask() const {
  unsigned mask = 0;
  if (values[TRANS_X] != 0 || values[TRANS_Y] != 0) {
    mask |= TranslateMask;
  }
  if (values[PERSP_0] != 0 || values[PERSP_1] != 0 || values[PERSP_2] != 1) {
    // Perspective projection non-linearly transforms coordinates, producing both scaling and
    // shearing effects - rectangles become trapezoids with non-uniform size changes.
    return static_cast<uint8_t>(mask | PerspectiveMask | AffineMask | ScaleMask);
  }

  int m00 = ScalarAs2sCompliment(values[SCALE_X]);
  int m01 = ScalarAs2sCompliment(values[SKEW_X]);
  int m10 = ScalarAs2sCompliment(values[SKEW_Y]);
  int m11 = ScalarAs2sCompliment(values[SCALE_Y]);
  if (m01 | m10) {
    // The skew components may be scale-inducing, unless we are dealing with a pure rotation.
    // Testing for a pure rotation is expensive, so we opt for being conservative by always setting
    // the scale bit. along with affine. By doing this, we are also ensuring that matrices have the
    // same type masks as their inverses.
    mask |= AffineMask | ScaleMask;

    // For rectStaysRect, in the affine case, we only need check that the primary diagonal is all
    // zeros and that the secondary diagonal is all non-zero.

    // map non-zero to 1
    m01 = m01 != 0;
    m10 = m10 != 0;
    int dp0 = 0 == (m00 | m11);
    int ds1 = m01 & m10;
    mask |= static_cast<unsigned>((dp0 & ds1) << RectStaysRectShift);
  } else {
    if ((m00 ^ Scalar1Int) | (m11 ^ Scalar1Int)) {
      mask |= ScaleMask;
    }

    // Not affine, therefore we already know secondary diagonal is all zeros, so we just need to
    // check that primary diagonal is all non-zero.

    // map non-zero to 1
    m00 = m00 != 0;
    m11 = m11 != 0;
    mask |= static_cast<unsigned>((m00 & m11) << RectStaysRectShift);
  }
  return static_cast<uint8_t>(mask);
}

void Matrix::IdentityPoints(const Matrix&, Point dst[], const Point src[], int count) {
  if (dst != src && count > 0) {
    memcpy(dst, src, sizeof(Point) * static_cast<size_t>(count));
  }
}

// Computes (a*b - c*d) * scale.
static inline float CrossDiffScale(float a, float b, float c, float d, float scale) {
  return (a * b - c * d) * scale;
}

float Matrix::CalcDeterminant(const Matrix& matrix, bool isPerspective) {
  auto& m = matrix.values;
  if (isPerspective) {
    return m[SCALE_X] * CrossDiffScale(m[SCALE_Y], m[PERSP_2], m[TRANS_Y], m[PERSP_1], 1) +
           m[SKEW_X] * CrossDiffScale(m[TRANS_Y], m[PERSP_0], m[SKEW_Y], m[PERSP_2], 1) +
           m[TRANS_X] * CrossDiffScale(m[SKEW_Y], m[PERSP_1], m[SCALE_Y], m[PERSP_0], 1);
  }
  return CrossDiffScale(m[SCALE_X], m[SCALE_Y], m[SKEW_X], m[SKEW_Y], 1);
}

void Matrix::ComputeInverse(Matrix& dst, const Matrix& src, float invDet, bool isPerspective) {
  // dst and src may be the same matrix, so compute all values first before assigning.
  auto& m = src.values;
  if (isPerspective) {
    auto sx = CrossDiffScale(m[SCALE_Y], m[PERSP_2], m[TRANS_Y], m[PERSP_1], invDet);
    auto kx = CrossDiffScale(m[TRANS_X], m[PERSP_1], m[SKEW_X], m[PERSP_2], invDet);
    auto tx = CrossDiffScale(m[SKEW_X], m[TRANS_Y], m[TRANS_X], m[SCALE_Y], invDet);
    auto ky = CrossDiffScale(m[TRANS_Y], m[PERSP_0], m[SKEW_Y], m[PERSP_2], invDet);
    auto sy = CrossDiffScale(m[SCALE_X], m[PERSP_2], m[TRANS_X], m[PERSP_0], invDet);
    auto ty = CrossDiffScale(m[TRANS_X], m[SKEW_Y], m[SCALE_X], m[TRANS_Y], invDet);
    auto p0 = CrossDiffScale(m[SKEW_Y], m[PERSP_1], m[SCALE_Y], m[PERSP_0], invDet);
    auto p1 = CrossDiffScale(m[SKEW_X], m[PERSP_0], m[SCALE_X], m[PERSP_1], invDet);
    auto p2 = CrossDiffScale(m[SCALE_X], m[SCALE_Y], m[SKEW_X], m[SKEW_Y], invDet);
    dst.setAll(sx, kx, tx, ky, sy, ty, p0, p1, p2);
  } else {
    auto sx = m[SCALE_Y] * invDet;
    auto kx = -m[SKEW_X] * invDet;
    auto ky = -m[SKEW_Y] * invDet;
    auto sy = m[SCALE_X] * invDet;
    auto tx = CrossDiffScale(m[SKEW_X], m[TRANS_Y], m[SCALE_Y], m[TRANS_X], invDet);
    auto ty = CrossDiffScale(m[SKEW_Y], m[TRANS_X], m[SCALE_X], m[TRANS_Y], invDet);
    dst.setAll(sx, kx, tx, ky, sy, ty);
  }
}

bool Matrix::invertNonIdentity(Matrix* inverse) const {
  TypeMask mask = this->getType();
  // Optimized invert for only scale and/or translation matrices.
  if (0 == (mask & ~(ScaleMask | TranslateMask))) {
    if (mask & ScaleMask) {
      if (values[SCALE_X] == 0 || values[SCALE_Y] == 0) {
        return false;
      }
      float invSX = 1 / values[SCALE_X];
      float invSY = 1 / values[SCALE_Y];
      float invTX = -values[TRANS_X] * invSX;
      float invTY = -values[TRANS_Y] * invSY;
      if (inverse) {
        inverse->values[SKEW_X] = inverse->values[SKEW_Y] = 0;
        inverse->values[SCALE_X] = invSX;
        inverse->values[SCALE_Y] = invSY;
        inverse->values[TRANS_X] = invTX;
        inverse->values[TRANS_Y] = invTY;
        inverse->values[PERSP_0] = inverse->values[PERSP_1] = 0;
        inverse->values[PERSP_2] = 1;
        inverse->setTypeMask(mask | RectStayRectMask);
      }
      return true;
    }
    if (inverse) {
      inverse->setTranslate(-values[TRANS_X], -values[TRANS_Y]);
    }
    return true;
  }
  const bool isPerspective = (mask & PerspectiveMask) != 0;
  float determinant = CalcDeterminant(*this, isPerspective);
  if (FloatNearlyZero(determinant, FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO)) {
    return false;
  }
  if (inverse) {
    ComputeInverse(*inverse, *this, 1.0f / determinant, isPerspective);
    inverse->setTypeMask(typeMask);
  }
  return true;
}

void Matrix::mapPoints(Point dst[], const Point src[], int count) const {
  this->getMapPtsProc()(*this, dst, src, count);
}

void Matrix::mapXY(float x, float y, Point* result) const {
  Point p(x, y);
  mapPoints(result, &p, 1);
}

bool Matrix::rectStaysRect() const {
  if (typeMask & UnknownMask) {
    typeMask = this->computeTypeMask();
  }
  return (typeMask & RectStayRectMask) != 0;
}

float Matrix::getMinScale() const {
  float results[2];
  if (getMinMaxScaleFactors(results)) {
    return results[0];
  }
  return 0.0f;
}

float Matrix::getMaxScale() const {
  float results[2];
  if (getMinMaxScaleFactors(results)) {
    return results[1];
  }
  return 0.0f;
}

Point Matrix::getAxisScales() const {
  Point scale = {};
  double sx = values[SCALE_X];
  double kx = values[SKEW_X];
  double ky = values[SKEW_Y];
  double sy = values[SCALE_Y];
  scale.x = static_cast<float>(sqrt(sx * sx + ky * ky));
  scale.y = static_cast<float>(sqrt(kx * kx + sy * sy));
  return scale;
}

bool Matrix::getMinMaxScaleFactors(float* results) const {
  float a = sdot(values[SCALE_X], values[SCALE_X], values[SKEW_Y], values[SKEW_Y]);
  float b = sdot(values[SCALE_X], values[SKEW_X], values[SCALE_Y], values[SKEW_Y]);
  float c = sdot(values[SKEW_X], values[SKEW_X], values[SCALE_Y], values[SCALE_Y]);
  float bSqd = b * b;
  if (bSqd <= FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO) {
    results[0] = a;
    results[1] = c;
    if (results[0] > results[1]) {
      using std::swap;
      swap(results[0], results[1]);
    }
  } else {
    float aminusc = a - c;
    float apluscdiv2 = (a + c) * 0.5f;
    float x = sqrtf(aminusc * aminusc + 4 * bSqd) * 0.5f;
    results[0] = apluscdiv2 - x;
    results[1] = apluscdiv2 + x;
  }
  auto isFinite = (results[0] * 0 == 0);
  if (!isFinite) {
    return false;
  }
  if (results[0] < 0) {
    results[0] = 0;
  }
  results[0] = sqrtf(results[0]);
  isFinite = (results[1] * 0 == 0);
  if (!isFinite) {
    return false;
  }
  if (results[1] < 0) {
    results[1] = 0;
  }
  results[1] = sqrtf(results[1]);
  return true;
}

bool Matrix::hasNonIdentityScale() const {
  double sx = values[SCALE_X];
  double ky = values[SKEW_Y];
  if (sqrt(sx * sx + ky * ky) != 1.0) {
    return true;
  }
  double kx = values[SKEW_X];
  double sy = values[SCALE_Y];
  if (sqrt(kx * kx + sy * sy) != 1.0) {
    return true;
  }
  return false;
}

bool Matrix::isFinite() const {
  return FloatsAreFinite(values, 9);
}

const Matrix& Matrix::I() {
  static const Matrix identity;
  return identity;
}

const Matrix::MapPtsProc Matrix::MapPtsProcs[] = {
    Matrix::IdentityPoints, Matrix::TransPoints,  Matrix::ScalePoints,  Matrix::ScalePoints,
    Matrix::AffinePoints,   Matrix::AffinePoints, Matrix::AffinePoints, Matrix::AffinePoints,
    Matrix::PerspPoints,    Matrix::PerspPoints,  Matrix::PerspPoints,  Matrix::PerspPoints,
    Matrix::PerspPoints,    Matrix::PerspPoints,  Matrix::PerspPoints,  Matrix::PerspPoints};
}  // namespace tgfx
