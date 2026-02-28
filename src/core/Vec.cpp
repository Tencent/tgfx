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

#include "tgfx/core/Vec.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

float Vec2::operator[](int i) const {
  DEBUG_ASSERT(i >= 0 && i < 2);
  return ptr()[i];
}

Vec2 operator/(const Vec2& v, float s) {
  return {IEEEFloatDivide(v.x, s), IEEEFloatDivide(v.y, s)};
}

bool Vec3::operator==(const Vec3& v) const {
  return FloatNearlyEqual(x, v.x) && FloatNearlyEqual(y, v.y) && FloatNearlyEqual(z, v.z);
}

bool Vec4::operator==(const Vec4& v) const {
  return FloatNearlyEqual(x, v.x) && FloatNearlyEqual(y, v.y) && FloatNearlyEqual(z, v.z) &&
         FloatNearlyEqual(w, v.w);
}

float Vec4::operator[](int i) const {
  DEBUG_ASSERT(i >= 0 && i < 4);
  return ptr()[i];
}

float& Vec4::operator[](int i) {
  DEBUG_ASSERT(i >= 0 && i < 4);
  return ptr()[i];
}

}  // namespace tgfx
