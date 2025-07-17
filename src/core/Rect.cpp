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
#include "SIMDVec.h"
#ifdef _MSC_VER
#include "SIMDHIghwayInterface.h"
#endif

namespace tgfx {
void Rect::scale(float scaleX, float scaleY) {
  left *= scaleX;
  right *= scaleX;
  top *= scaleY;
  bottom *= scaleY;
}

bool Rect::setBounds(const Point pts[], int count) {
#ifdef _MSC_VER
  return SetBoundsHWY(this, pts, count);
#else
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
