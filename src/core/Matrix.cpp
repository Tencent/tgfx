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
#include "SIMDVec.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

void Matrix::reset() {
  *this = Matrix();
}

bool operator==(const Matrix& a, const Matrix& b) {
  const float* ma = a.values;
  const float* mb = b.values;
  return ma[0] == mb[0] && ma[1] == mb[1] && ma[2] == mb[2] && ma[3] == mb[3] && ma[4] == mb[4] &&
         ma[5] == mb[5];
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
  this->setTypeMask(UnknownMask);
}

void Matrix::get9(float buffer[9]) const {
  memcpy(buffer, values, 6 * sizeof(float));
  buffer[6] = buffer[7] = 0.0f;
  buffer[8] = 1.0f;
}

void Matrix::setTranslate(float tx, float ty) {
  if ((tx != 0) | (ty != 0)) {
    values[TRANS_X] = tx;
    values[TRANS_Y] = ty;

    values[SCALE_X] = values[SCALE_Y] = 1;
    values[SKEW_X] = values[SKEW_Y] = 0;
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
  values[TRANS_X] += tx;
  values[TRANS_Y] += ty;
  this->updateTranslateMask();
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
  values[SKEW_X] *= sy;
  values[SCALE_Y] *= sy;
  if (values[SCALE_X] == 1 && values[SCALE_Y] == 1 && !(typeMask & AffineMask)) {
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
  this->setTypeMask(UnknownMask);
}

void Matrix::setSinCos(float sinV, float cosV) {
  values[SCALE_X] = cosV;
  values[SKEW_X] = -sinV;
  values[TRANS_X] = 0;
  values[SKEW_Y] = sinV;
  values[SCALE_Y] = cosV;
  values[TRANS_Y] = 0;
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
  this->setTypeMask(UnknownMask);
}

void Matrix::setSkew(float kx, float ky) {
  values[SCALE_X] = 1;
  values[SKEW_X] = kx;
  values[TRANS_X] = 0;
  values[SKEW_Y] = ky;
  values[SCALE_Y] = 1;
  values[TRANS_Y] = 0;
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
  return 0 == (mask & Matrix::AffineMask);
}

void Matrix::setConcat(const Matrix& first, const Matrix& second) {
  auto& matrixA = first.values;
  auto& matrixB = second.values;
  TypeMask aType = first.getType();
  TypeMask bType = second.getType();
  auto sx = matrixB[SCALE_X] * matrixA[SCALE_X];
  auto kx = 0.0f;
  auto ky = 0.0f;
  auto sy = matrixB[SCALE_Y] * matrixA[SCALE_Y];
  auto tx = matrixB[TRANS_X] * matrixA[SCALE_X] + matrixA[TRANS_X];
  auto ty = matrixB[TRANS_Y] * matrixA[SCALE_Y] + matrixA[TRANS_Y];
  if (first.isTriviallyIdentity()) {
    *this = second;
  } else if (second.isTriviallyIdentity()) {
    *this = first;
  } else if (OnlyScaleAndTranslate(aType | bType)) {
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

void Matrix::TransPoints(const Matrix& m, Point dst[], const Point src[], int count) {
  if (count > 0) {
    float tx = m.getTranslateX();
    float ty = m.getTranslateY();
    if (count & 1) {
      dst->x = src->x + tx;
      dst->y = src->y + ty;
      dst += 1;
      src += 1;
    }
    float4 trans4(tx, ty, tx, ty);
    count >>= 1;
    if (count & 1) {
      (float4::Load(src) + trans4).store(dst);
      src += 2;
      dst += 2;
    }
    count >>= 1;
    for (int i = 0; i < count; i++) {
      (float4::Load(src) + trans4).store(dst);
      (float4::Load(src + 2) + trans4).store(dst + 2);
      src += 4;
      dst += 4;
    }
  }
}

void Matrix::ScalePoints(const Matrix& m, Point dst[], const Point src[], int count) {
  if (count > 0) {
    float tx = m.getTranslateX();
    float ty = m.getTranslateY();
    float sx = m.getScaleX();
    float sy = m.getScaleY();
    float4 trans4(tx, ty, tx, ty);
    float4 scale4(sx, sy, sx, sy);
    if (count & 1) {
      float4 p(src->x, src->y, 0, 0);
      p = p * scale4 + trans4;
      dst->x = p[0];
      dst->y = p[1];
      src += 1;
      dst += 1;
    }
    count >>= 1;
    if (count & 1) {
      (float4::Load(src) * scale4 + trans4).store(dst);
      src += 2;
      dst += 2;
    }
    count >>= 1;
    for (int i = 0; i < count; i++) {
      (float4::Load(src) * scale4 + trans4).store(dst);
      (float4::Load(src + 2) * scale4 + trans4).store(dst + 2);
      src += 4;
      dst += 4;
    }
  }
}

void Matrix::AfflinePoints(const Matrix& m, Point dst[], const Point src[], int count) {
  if (count > 0) {
    float tx = m.getTranslateX();
    float ty = m.getTranslateY();
    float sx = m.getScaleX();
    float sy = m.getScaleY();
    float kx = m.getSkewX();
    float ky = m.getSkewY();
    float4 trans4(tx, ty, tx, ty);
    float4 scale4(sx, sy, sx, sy);
    float4 skew4(kx, ky, kx, ky);
    bool trailingElement = (count & 1);
    count >>= 1;
    float4 src4;
    for (int i = 0; i < count; i++) {
      src4 = float4::Load(src);
      // y0, x0, y1, x1
      float4 swz4 = Shuffle<1, 0, 3, 2>(src4);
      (src4 * scale4 + swz4 * skew4 + trans4).store(dst);
      src += 2;
      dst += 2;
    }
    if (trailingElement) {
      // We use the same logic here to ensure that the math stays consistent throughout, even
      // though the high float2 is ignored.
      src4.lo = float2::Load(src);
      float4 swz4 = Shuffle<1, 0, 3, 2>(src4);
      (src4 * scale4 + swz4 * skew4 + trans4).lo.store(dst);
    }
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
        inverse->setTypeMask(mask | RectStayRectMask);
      }
      return true;
    }
    if (inverse) {
      inverse->setTranslate(-values[TRANS_X], -values[TRANS_Y]);
    }
    return true;
  }
  float determinant = values[SCALE_X] * values[SCALE_Y] - values[SKEW_X] * values[SKEW_Y];
  if (FloatNearlyZero(determinant, FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO)) {
    return false;
  }
  determinant = 1 / determinant;
  auto sx = values[SCALE_Y] * determinant;
  auto ky = -values[SKEW_Y] * determinant;
  auto kx = -values[SKEW_X] * determinant;
  auto sy = values[SCALE_X] * determinant;
  auto tx = -(sx * values[TRANS_X] + kx * values[TRANS_Y]);
  auto ty = -(ky * values[TRANS_X] + sy * values[TRANS_Y]);
  if (inverse) {
    inverse->setAll(sx, kx, tx, ky, sy, ty);
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

static float4 SortAsRect(const float4& ltrb) {
  float4 rblt(ltrb[2], ltrb[3], ltrb[0], ltrb[1]);
  auto min = Min(ltrb, rblt);
  auto max = Max(ltrb, rblt);
  // We can extract either pair [0,1] or [2,3] from min and max and be correct, but on ARM this
  // sequence generates the fastest (a single instruction).
  return float4(min[2], min[3], max[0], max[1]);
}

void Matrix::mapRect(Rect* dst, const Rect& src) const {
  if (this->getType() <= TranslateMask) {
    float tx = values[TRANS_X];
    float ty = values[TRANS_Y];
    float4 trans(tx, ty, tx, ty);
    SortAsRect(float4::Load(&src.left) + trans).store(&dst->left);
    return;
  }
  if (this->isScaleTranslate()) {
    float sx = values[SCALE_X];
    float sy = values[SCALE_Y];
    float tx = values[TRANS_X];
    float ty = values[TRANS_Y];
    float4 scale(sx, sy, sx, sy);
    float4 trans(tx, ty, tx, ty);
    SortAsRect(float4::Load(&src.left) * scale + trans).store(&dst->left);
  } else {
    Point quad[4];
    quad[0].set(src.left, src.top);
    quad[1].set(src.right, src.top);
    quad[2].set(src.right, src.bottom);
    quad[3].set(src.left, src.bottom);
    mapPoints(quad, quad, 4);
    dst->setBounds(quad, 4);
  }
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
  return FloatsAreFinite(values, 6);
}

const Matrix& Matrix::I() {
  static const Matrix identity;
  return identity;
}

const Matrix::MapPtsProc Matrix::MapPtsProcs[] = {
    Matrix::IdentityPoints, Matrix::TransPoints,   Matrix::ScalePoints,   Matrix::ScalePoints,
    Matrix::AfflinePoints,  Matrix::AfflinePoints, Matrix::AfflinePoints, Matrix::AfflinePoints};
}  // namespace tgfx
