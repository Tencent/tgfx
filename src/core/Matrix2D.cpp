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
#include <algorithm>
#include <cmath>
#include <limits>
#include "utils/MathExtra.h"

namespace tgfx {

// Minimum w value for perspective clipping to avoid division by near-zero values.
static constexpr float W0_PLANE_DISTANCE = 1.f / (1 << 14);

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

// Clips an edge against the w=0 plane. Returns the intersection point's contribution to the
// bounding box as (x, y, -x, -y). If both endpoints are behind the camera, returns infinity.
static Vec4 ClipEdgeToW0Plane(const Vec3& p0, const Vec3& p1) {
  constexpr float inf = std::numeric_limits<float>::infinity();
  const float w0 = p0.z;
  const float w1 = p1.z;
  if (w1 >= W0_PLANE_DISTANCE) {
    const float t = (W0_PLANE_DISTANCE - w0) / (w1 - w0);
    const float cx = (t * p1.x + (1.f - t) * p0.x) / W0_PLANE_DISTANCE;
    const float cy = (t * p1.y + (1.f - t) * p0.y) / W0_PLANE_DISTANCE;
    return {cx, cy, -cx, -cy};
  }
  return {inf, inf, inf, inf};
}

// Projects a corner point with perspective clipping. p0 is the current corner, p1 and p2 are its
// two adjacent corners. Returns (minX, minY, -maxX, -maxY) for bounding box accumulation.
static Vec4 ProjectCornerWithClip(const Vec3& p0, const Vec3& p1, const Vec3& p2) {
  const float w0 = p0.z;
  if (w0 >= W0_PLANE_DISTANCE) {
    const float invW = 1.f / w0;
    const float x = p0.x * invW;
    const float y = p0.y * invW;
    return {x, y, -x, -y};
  }
  // Point is behind the camera, clip both edges and take the min of results
  auto c1 = ClipEdgeToW0Plane(p0, p1);
  auto c2 = ClipEdgeToW0Plane(p0, p2);
  return {std::min(c1.x, c2.x), std::min(c1.y, c2.y), std::min(c1.z, c2.z), std::min(c1.w, c2.w)};
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
  if (hasPerspective()) {
    return mapRectPerspective(src);
  }
  return mapRectAffine(src);
}

Rect Matrix2D::mapRectAffine(const Rect& src) const {
  auto lt = mapVec2({src.left, src.top});
  auto rt = mapVec2({src.right, src.top});
  auto lb = mapVec2({src.left, src.bottom});
  auto rb = mapVec2({src.right, src.bottom});
  return Rect::MakeLTRB(std::min({lt.x, rt.x, lb.x, rb.x}), std::min({lt.y, rt.y, lb.y, rb.y}),
                        std::max({lt.x, rt.x, lb.x, rb.x}), std::max({lt.y, rt.y, lb.y, rb.y}));
}

Rect Matrix2D::mapRectPerspective(const Rect& src) const {
  auto tl = mapPoint(src.left, src.top, 1);
  auto tr = mapPoint(src.right, src.top, 1);
  auto bl = mapPoint(src.left, src.bottom, 1);
  auto br = mapPoint(src.right, src.bottom, 1);

  // Project all 4 corners with their adjacent vertices for clipping
  // Rectangle adjacency: TL <-> TR <-> BR <-> BL <-> TL
  auto pTL = ProjectCornerWithClip(tl, tr, bl);
  auto pTR = ProjectCornerWithClip(tr, br, tl);
  auto pBR = ProjectCornerWithClip(br, bl, tr);
  auto pBL = ProjectCornerWithClip(bl, tl, br);

  // Accumulate min/max across all corners. Result format: (minX, minY, -maxX, -maxY)
  auto minMax =
      Vec4{std::min({pTL.x, pTR.x, pBR.x, pBL.x}), std::min({pTL.y, pTR.y, pBR.y, pBL.y}),
           std::min({pTL.z, pTR.z, pBR.z, pBL.z}), std::min({pTL.w, pTR.w, pBR.w, pBL.w})};
  return Rect::MakeLTRB(minMax.x, minMax.y, -minMax.z, -minMax.w);
}

Vec2 Matrix2D::mapVec2(const Vec2& v) const {
  auto r = mapPoint(v.x, v.y, 1);
  return {IEEEFloatDivide(r.x, r.z), IEEEFloatDivide(r.y, r.z)};
}

Vec3 Matrix2D::mapPoint(float x, float y, float w) const {
  auto c0 = getCol(0);
  auto c1 = getCol(1);
  auto c2 = getCol(2);
  return c0 * x + c1 * y + c2 * w;
}

}  // namespace tgfx
