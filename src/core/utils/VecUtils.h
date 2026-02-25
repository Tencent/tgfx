/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/core/Vec.h"

namespace tgfx {

class VecUtils {
 public:
  /**
   * Component-wise greater-than-or-equal comparison with scalar. Returns a Vec4 where each
   * component is 1.0f if v[i] >= s, 0.0f otherwise.
   */
  static Vec4 GreaterEqual(const Vec4& v, float s);

  /**
   * Component-wise less-than comparison with scalar. Returns a Vec4 where each component is
   * 1.0f if v[i] < s, 0.0f otherwise.
   */
  static Vec4 LessThan(const Vec4& v, float s);

  /**
   * Component-wise logical OR. Returns a Vec4 where each component is 1.0f if either operand
   * is non-zero, 0.0f otherwise.
   */
  static Vec4 Or(const Vec4& a, const Vec4& b);

  /**
   * Component-wise logical AND. Returns a Vec4 where each component is 1.0f if both operands
   * are non-zero, 0.0f otherwise.
   */
  static Vec4 And(const Vec4& a, const Vec4& b);

  /**
   * Returns a vector containing the square root of each component.
   */
  static Vec4 Sqrt(const Vec4& v);

  /**
   * Returns a vector containing the absolute value of each component.
   */
  static Vec4 Abs(const Vec4& v);

  /**
   * Returns t[i] if cond[i] != 0, otherwise e[i].
   */
  static Vec4 IfThenElse(const Vec4& cond, const Vec4& t, const Vec4& e);

  /**
   * Returns true if any component of the vector is non-zero.
   */
  static bool Any(const Vec4& v);

  /**
   * Returns true if all components of the vector are non-zero.
   */
  static bool All(const Vec4& v);

  /**
   * Shuffles the components of a Vec2 into a Vec4.
   * @tparam Ix Indices specifying which components to use from the Vec2. Valid values are 0 and 1.
   */
  template <int... Ix>
  static inline Vec4 Shuffle(const Vec2& v) {
    static_assert(sizeof...(Ix) == 4, "Vec2 to Vec4 shuffle requires exactly 4 indices");
    return {v[Ix]...};
  }

  /**
   * Shuffles the components of a Vec4 into a new Vec4.
   * @tparam Ix Indices specifying which components to use. Valid values are 0, 1, 2, and 3.
   */
  template <int... Ix>
  static inline Vec4 Shuffle(const Vec4& v) {
    static_assert(sizeof...(Ix) == 4, "Vec4 to Vec4 shuffle requires exactly 4 indices");
    return {v[Ix]...};
  }
};

}  // namespace tgfx
