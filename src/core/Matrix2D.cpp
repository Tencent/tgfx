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

#include "Matrix2D.h"
#include "utils/MathExtra.h"

namespace tgfx {

static bool InvertMatrix2D(const float inMat[9], float outMat[9]) {
  // a[ij] represents the element at column i and row j
  const float a00 = inMat[0];
  const float a01 = inMat[1];
  const float a02 = inMat[2];
  const float a10 = inMat[3];
  const float a11 = inMat[4];
  const float a12 = inMat[5];
  const float a20 = inMat[6];
  const float a21 = inMat[7];
  const float a22 = inMat[8];

  // Calculate the cofactor, which is the determinant after excluding a specific row and column from
  // the matrix
  const float b00 = a11 * a22 - a12 * a21;
  const float b01 = a10 * a22 - a12 * a20;
  const float b02 = a10 * a21 - a11 * a20;

  const float determinant = a00 * b00 - a01 * b01 + a02 * b02;
  if (FloatNearlyZero(determinant, FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO)) {
    return false;
  }

  const float invdet = 1.f / determinant;
  outMat[0] = b00 * invdet;
  outMat[1] = -(a01 * a22 - a02 * a21) * invdet;
  outMat[2] = (a01 * a12 - a02 * a11) * invdet;
  outMat[3] = -b01 * invdet;
  outMat[4] = (a00 * a22 - a02 * a20) * invdet;
  outMat[5] = -(a00 * a12 - a02 * a10) * invdet;
  outMat[6] = b02 * invdet;
  outMat[7] = -(a00 * a21 - a01 * a20) * invdet;
  outMat[8] = (a00 * a11 - a01 * a10) * invdet;

  if (!FloatsAreFinite(outMat, 9)) {
    return false;
  }
  return true;
}

bool Matrix2D::invert(Matrix2D* inverse) const {
  float result[9];
  if (!InvertMatrix2D(values, result)) {
    return false;
  }
  if (inverse != nullptr) {
    memcpy(inverse->values, result, sizeof(result));
  }
  return true;
}

Rect Matrix2D::mapRect(const Rect& src) const {
  const Vec2 srcLT(src.left, src.top);
  const Vec2 srcLB(src.left, src.bottom);
  const Vec2 srcRT(src.right, src.top);
  const Vec2 srcRB(src.right, src.bottom);

  auto ltDst = mapVec2(srcLT);
  auto lbDst = mapVec2(srcLB);
  auto rtDst = mapVec2(srcRT);
  auto rbDst = mapVec2(srcRB);

  auto minX = std::min({ltDst.x, lbDst.x, rtDst.x, rbDst.x});
  auto maxX = std::max({ltDst.x, lbDst.x, rtDst.x, rbDst.x});
  auto minY = std::min({ltDst.y, lbDst.y, rtDst.y, rbDst.y});
  auto maxY = std::max({ltDst.y, lbDst.y, rtDst.y, rbDst.y});
  return Rect::MakeLTRB(minX, minY, maxX, maxY);
}

Vec2 Matrix2D::mapVec2(const Vec2& v) const {
  auto r = this->mapPoint(v.x, v.y, 1.f);
  return {IEEEFloatDivide(r.x, r.z), IEEEFloatDivide(r.y, r.z)};
}

Vec3 Matrix2D::mapPoint(float x, float y, float w) const {
  auto c0 = getCol(0);
  auto c1 = getCol(1);
  auto c2 = getCol(2);

  const Vec3 result = (c0 * x + c1 * y + c2 * w);
  return result;
}

}  // namespace tgfx
