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

#pragma once

namespace tgfx {
/**
 * Vector3 holds three 32-bit floating point coordinates.
 */
struct Vector3 {
  /**
   * Creates a Vector3 with specified x, y and z value.
   */
  static constexpr Vector3 Make(float x, float y, float z) {
    return {x, y, z};
  }

  /**
   * Constructs a Vector3 set to (0, 0, 0).
   */
  constexpr Vector3() : x(0), y(0), z(0) {
  }

  /**
  * Constructs a Vector3 set to (x, y, z).
  */
  constexpr Vector3(float x, float y, float z) : x(x), y(y), z(z) {
  }

  /**
   * Returns true if current vector is equivalent to v.
   */
  bool operator==(const Vector3& v) const {
    return x == v.x && y == v.y && z == v.z;
  }

  /**
   * Returns true if current vector is not equivalent to v.
   */
  bool operator!=(const Vector3& v) const {
    return !(*this == v);
  }

  /**
   * Returns the sum of the current vector and v.
   */
  Vector3 operator+(const Vector3& v) const {
    return {x + v.x, y + v.y, z + v.z};
  }

  /**
   * Returns the sum of the current vector and a scalar value.
   * This adds the value to each component of the vector.
   */
  Vector3 operator+(const float scalar) const {
    return {x + scalar, y + scalar, z + scalar};
  }

  /**
   * Returns the difference between the current vector and v.
   */
  Vector3 operator-(const Vector3& v) const {
    return {x - v.x, y - v.y, z - v.z};
  }

  /**
   * Returns the current vector scaled by s.
   */
  Vector3 operator*(const float s) const {
    return {x * s, y * s, z * s};
  }

  /**
   * Returns the current vector divided by s.
   */
  Vector3 operator/(const float s) const {
    return {x / s, y / s, z / s};
  }

  float x, y, z;
};

}  // namespace tgfx
