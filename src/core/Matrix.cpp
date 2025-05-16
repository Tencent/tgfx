/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <array>
#include <cfloat>
#include "core/utils/MathExtra.h"

namespace tgfx {

void Matrix::reset() {
  values[SCALE_X] = values[SCALE_Y] = 1;
  values[SKEW_X] = values[SKEW_Y] = values[TRANS_X] = values[TRANS_Y] = 0;
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
  } else {
    this->reset();
  }
}

inline float sdot(float a, float b, float c, float d) {
  return a * b + c * d;
}

void Matrix::preTranslate(float tx, float ty) {
  values[TRANS_X] += sdot(values[SCALE_X], tx, values[SKEW_X], ty);
  values[TRANS_Y] += sdot(values[SKEW_Y], tx, values[SCALE_Y], ty);
}

void Matrix::postTranslate(float tx, float ty) {
  values[TRANS_X] += tx;
  values[TRANS_Y] += ty;
}

void Matrix::setScale(float sx, float sy, float px, float py) {
  if (1 == sx && 1 == sy) {
    this->reset();
  } else {
    values[SCALE_X] = sx;
    values[SKEW_X] = 0;
    values[TRANS_X] = px - sx * px;
    values[SKEW_Y] = 0;
    values[SCALE_Y] = sy;
    values[TRANS_Y] = py - sy * py;
  }
}

void Matrix::setScale(float sx, float sy) {
  if (1 == sx && 1 == sy) {
    this->reset();
  } else {
    values[SCALE_X] = sx;
    values[SCALE_Y] = sy;
    values[TRANS_X] = values[TRANS_Y] = values[SKEW_X] = values[SKEW_Y] = 0;
  }
}

void Matrix::preScale(float sx, float sy, float px, float py) {
  if (1 == sx && 1 == sy) {
    return;
  }
  this->preConcat({sx, 0, px - sx * px, 0, sy, py - sy * py});
}

void Matrix::preScale(float sx, float sy) {
  if (1 == sx && 1 == sy) {
    return;
  }
  values[SCALE_X] *= sx;
  values[SKEW_Y] *= sx;
  values[SKEW_X] *= sy;
  values[SCALE_Y] *= sy;
}

void Matrix::postScale(float sx, float sy, float px, float py) {
  if (1 == sx && 1 == sy) {
    return;
  }
  this->postConcat({sx, 0, px - sx * px, 0, sy, py - sy * py});
}

void Matrix::postScale(float sx, float sy) {
  if (1 == sx && 1 == sy) {
    return;
  }
  this->postConcat({sx, 0, 0, 0, sy, 0});
}

void Matrix::setSinCos(float sinV, float cosV, float px, float py) {
  const float oneMinusCosV = 1 - cosV;
  values[SCALE_X] = cosV;
  values[SKEW_X] = -sinV;
  values[TRANS_X] = sdot(sinV, py, oneMinusCosV, px);
  values[SKEW_Y] = sinV;
  values[SCALE_Y] = cosV;
  values[TRANS_Y] = sdot(-sinV, px, oneMinusCosV, py);
}

void Matrix::setSinCos(float sinV, float cosV) {
  values[SCALE_X] = cosV;
  values[SKEW_X] = -sinV;
  values[TRANS_X] = 0;
  values[SKEW_Y] = sinV;
  values[SCALE_Y] = cosV;
  values[TRANS_Y] = 0;
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
}

void Matrix::setSkew(float kx, float ky) {
  values[SCALE_X] = 1;
  values[SKEW_X] = kx;
  values[TRANS_X] = 0;
  values[SKEW_Y] = ky;
  values[SCALE_Y] = 1;
  values[TRANS_Y] = 0;
}

void Matrix::preSkew(float kx, float ky, float px, float py) {
  this->preConcat({1, kx, -kx * py, ky, 1, -ky * px});
}

void Matrix::preSkew(float kx, float ky) {
  this->preConcat({1, kx, 0, ky, 1, 0});
}

void Matrix::postSkew(float kx, float ky, float px, float py) {
  this->postConcat({1, kx, -kx * py, ky, 1, -ky * px});
}

void Matrix::postSkew(float kx, float ky) {
  this->postConcat({1, kx, 0, ky, 1, 0});
}

void Matrix::setConcat(const Matrix& first, const Matrix& second) {
  auto& matrixA = first.values;
  auto& matrixB = second.values;
  auto sx = matrixB[SCALE_X] * matrixA[SCALE_X];
  auto kx = 0.0f;
  auto ky = 0.0f;
  auto sy = matrixB[SCALE_Y] * matrixA[SCALE_Y];
  auto tx = matrixB[TRANS_X] * matrixA[SCALE_X] + matrixA[TRANS_X];
  auto ty = matrixB[TRANS_Y] * matrixA[SCALE_Y] + matrixA[TRANS_Y];

  if (matrixB[SKEW_Y] != 0.0 || matrixB[SKEW_X] != 0.0 || matrixA[SKEW_Y] != 0.0 ||
      matrixA[SKEW_X] != 0.0) {
    sx += matrixB[SKEW_Y] * matrixA[SKEW_X];
    sy += matrixB[SKEW_X] * matrixA[SKEW_Y];
    ky += matrixB[SCALE_X] * matrixA[SKEW_Y] + matrixB[SKEW_Y] * matrixA[SCALE_Y];
    kx += matrixB[SKEW_X] * matrixA[SCALE_X] + matrixB[SCALE_Y] * matrixA[SKEW_X];
    tx += matrixB[TRANS_Y] * matrixA[SKEW_X];
    ty += matrixB[TRANS_X] * matrixA[SKEW_Y];
  }
  setAll(sx, kx, tx, ky, sy, ty);
}

void Matrix::preConcat(const Matrix& matrix) {
  // check for identity first, so we don't do a needless copy of ourselves
  // to ourselves inside setConcat()
  if (!matrix.isIdentity()) {
    this->setConcat(*this, matrix);
  }
}

void Matrix::postConcat(const Matrix& matrix) {
  // check for identity first, so we don't do a needless copy of ourselves
  // to ourselves inside setConcat()
  if (!matrix.isIdentity()) {
    this->setConcat(matrix, *this);
  }
}

bool Matrix::invertible() const {
  float determinant = values[SCALE_X] * values[SCALE_Y] - values[SKEW_Y] * values[SKEW_X];
  return !(FloatNearlyZero(determinant, FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO));
}

bool Matrix::invertNonIdentity(Matrix* inverse) const {
  auto sx = values[SCALE_X];
  auto kx = values[SKEW_X];
  auto ky = values[SKEW_Y];
  auto sy = values[SCALE_Y];
  auto tx = values[TRANS_X];
  auto ty = values[TRANS_Y];

  if (ky == 0 && kx == 0) {
    if (sx == 0 || sy == 0) {
      return false;
    }
    sx = 1 / sx;
    sy = 1 / sy;
    tx = -sx * tx;
    ty = -sy * ty;
    inverse->setAll(sx, kx, tx, ky, sy, ty);
    return true;
  }
  float determinant = sx * sy - ky * kx;
  if (FloatNearlyZero(determinant, FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO)) {
    return false;
  }
  determinant = 1 / determinant;
  sx = sy * determinant;
  ky = -ky * determinant;
  kx = -kx * determinant;
  sy = values[SCALE_X] * determinant;
  tx = -(sx * values[TRANS_X] + kx * values[TRANS_Y]);
  ty = -(ky * values[TRANS_X] + sy * values[TRANS_Y]);
  inverse->setAll(sx, kx, tx, ky, sy, ty);
  return true;
}

void Matrix::mapPoints(Point dst[], const Point src[], int count) const {
  auto tx = values[TRANS_X];
  auto ty = values[TRANS_Y];
  auto sx = values[SCALE_X];
  auto sy = values[SCALE_Y];
  auto kx = values[SKEW_X];
  auto ky = values[SKEW_Y];
  for (int i = 0; i < count; i++) {
    auto x = src[i].x * sx + src[i].y * kx + tx;
    auto y = src[i].x * ky + src[i].y * sy + ty;
    dst[i].set(x, y);
  }
}

void Matrix::mapXY(float x, float y, Point* result) const {
  auto tx = values[TRANS_X];
  auto ty = values[TRANS_Y];
  auto sx = values[SCALE_X];
  auto sy = values[SCALE_Y];
  auto kx = values[SKEW_X];
  auto ky = values[SKEW_Y];
  result->set(x * sx + y * kx + tx, x * ky + y * sy + ty);
}

bool Matrix::rectStaysRect() const {
  float sx = values[SCALE_X];
  float kx = values[SKEW_X];
  float ky = values[SKEW_Y];
  float sy = values[SCALE_Y];
  if (kx != 0 || ky != 0) {
    return sx == 0 && sy == 0 && ky != 0 && kx != 0;
  }
  return sx != 0 && sy != 0;
}

void Matrix::mapRect(Rect* dst, const Rect& src) const {
  Point quad[4];
  quad[0].set(src.left, src.top);
  quad[1].set(src.right, src.top);
  quad[2].set(src.right, src.bottom);
  quad[3].set(src.left, src.bottom);
  mapPoints(quad, quad, 4);
  dst->setBounds(quad, 4);
}

float Matrix::getMinScale() const {
  float results[2];
  if (getMinMaxScaleFactors(results)) {
    return results[0];
  }
  return -1.0f;
}

float Matrix::getMaxScale() const {
  float results[2];
  if (getMinMaxScaleFactors(results)) {
    return results[1];
  }
  return -1.0f;
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

bool Matrix::isTranslate() const {
  return values[SCALE_X] == 1 && values[SCALE_Y] == 1 && values[SKEW_X] == 0 && values[SKEW_Y] == 0;
}

bool Matrix::isFinite() const {
  return FloatsAreFinite(values, 6);
}

const Matrix& Matrix::I() {
  static const Matrix identity = Matrix::MakeAll(1, 0, 0, 0, 1, 0);
  return identity;
}

std::array<float, 6> Matrix::asAffine() const {
  static constexpr int AFFINE_SCALE_X = 0;
  static constexpr int AFFINE_SKEW_Y = 1;
  static constexpr int AFFINE_SKEW_X = 2;
  static constexpr int AFFINE_SCALE_Y = 3;
  static constexpr int AFFINE_TRANS_X = 4;
  static constexpr int AFFINE_TRANS_Y = 5;

  std::array<float, 6> affine;
  affine[AFFINE_SCALE_X] = values[SCALE_X];
  affine[AFFINE_SKEW_Y] = values[SKEW_Y];
  affine[AFFINE_SKEW_X] = values[SKEW_X];
  affine[AFFINE_SCALE_Y] = values[SCALE_Y];
  affine[AFFINE_TRANS_X] = values[TRANS_X];
  affine[AFFINE_TRANS_Y] = values[TRANS_Y];
  return affine;
}

}  // namespace tgfx
