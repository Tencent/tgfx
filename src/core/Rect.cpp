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

#include "tgfx/core/Rect.h"
#include <xsimd/xsimd.hpp>
#include "MathDynamic.h"
#include "SIMDVec.h"
#include "condition.h"
namespace tgfx {
void Rect::scale(float scaleX, float scaleY) {
  left *= scaleX;
  right *= scaleX;
  top *= scaleY;
  bottom *= scaleY;
}

bool Rect::setBounds(const Point pts[], int count) {
#ifdef NSIMD
  if (count <= 0) {
    this->setEmpty();
    return true;
  }
  float minX, maxX;
  float minY, maxY;
  minX = maxX = pts[0].x;
  minY = maxY = pts[0].y;

  for (int i = 1; i < count; i++) {
    auto x = pts[i].x;
    auto y = pts[i].y;
    auto isFinite = ((x + y) * 0 == 0);
    if (!isFinite) {
      setEmpty();
      return false;
    }
    if (x < minX) {
      minX = x;
    }
    if (x > maxX) {
      maxX = x;
    }
    if (y < minY) {
      minY = y;
    }
    if (y > maxY) {
      maxY = y;
    }
  }
  setLTRB(minX, minY, maxX, maxY);
  return true;
#endif
#ifdef SKSIMD
  if (count <= 0) {
    this->setEmpty();
    return false;
  }
  float4 min, max;
  if (count & 1) {
    min = max = float2::Load(pts).xyxy();
    pts += 1;
    count -= 1;
  } else {
    min = max = float4::Load(pts);
    pts += 2;
    count -= 2;
  }
  float4 accum = min * 0.0f;
  while (count) {
    float4 xy = float4::Load(pts);
    accum = accum * xy;
    min = Min(min, xy);
    max = Max(max, xy);
    pts += 2;
    count -= 2;
  }
  const bool allFinite = All(accum * 0.0f == 0.0f);
  if (allFinite) {
    this->setLTRB(std::min(min[0], min[2]), std::min(min[1], min[3]), std::max(max[0], max[2]),
                  std::max(max[1], max[3]));
    return true;
  } else {
    this->setEmpty();
    return false;
  }
#endif
#ifdef XSIMD
  using simdType = xsimd::batch<float>;
  if (count <= 0) {
    this->setEmpty();
    return false;
  }
  simdType min, max;
  if(count & 1) {
    min = max = simdType(pts[0].x, pts[0].y, pts[0].x, pts[0].y);
    pts += 1;
    count -= 1;
  }else {
    min = max = simdType(pts[0].x, pts[0].y, pts[1].x, pts[1].y);
    pts += 2;
    count -= 2;
  }
  simdType accum = min * simdType(0.0f);
  while(count) {
    simdType xy = simdType(pts[0].x, pts[0].y, pts[1].x, pts[1].y);
    accum = accum * xy;
    min = xsimd::min(min, xy);
    max = xsimd::max(max, xy);
    pts += 2;
    count -= 2;
  }
  const bool allFinite = xsimd::all(accum * simdType(0) == simdType(0));
  if(allFinite) {
    this->setLTRB(std::min(min.get(0), min.get(2)), std::min(min.get(1), min.get(3)), std::max(max.get(0), max.get(2)),
      std::max(max.get(1),max.get(3)));
    return true;
  }else {
    this->setEmpty();
    return false;
  }

#endif
#ifdef HIGHWAY
  return SetBoundsDynamic(this, pts, count);
#endif
}

#define CHECK_INTERSECT(al, at, ar, ab, bl, bt, br, bb) \
  float L = al > bl ? al : bl;                          \
  float R = ar < br ? ar : br;                          \
  float T = at > bt ? at : bt;                          \
  float B = ab < bb ? ab : bb;                          \
  do {                                                  \
    if (!(L < R && T < B)) {                            \
      return false;                                     \
    }                                                   \
  } while (0)
// do the !(opposite) check so we return false if either arg is NaN

bool Rect::intersect(float l, float t, float r, float b) {
  CHECK_INTERSECT(l, t, r, b, left, top, right, bottom);
  this->setLTRB(L, T, R, B);
  return true;
}

bool Rect::intersect(const Rect& a, const Rect& b) {
  CHECK_INTERSECT(a.left, a.top, a.right, a.bottom, b.left, b.top, b.right, b.bottom);
  this->setLTRB(L, T, R, B);
  return true;
}

void Rect::join(float l, float t, float r, float b) {
  // do nothing if the params are empty
  if (l >= r || t >= b) {
    return;
  }
  // if we are empty, just assign
  if (left >= right || top >= bottom) {
    this->setLTRB(l, t, r, b);
  } else {
    left = left < l ? left : l;
    top = top < t ? top : t;
    right = right > r ? right : r;
    bottom = bottom > b ? bottom : b;
  }
}
}  // namespace tgfx
