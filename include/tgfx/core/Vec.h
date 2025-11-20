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

#include <cmath>
#include <cstring>

namespace tgfx {

/**
 * Vec2 represents a two-dimensional vector with x and y components.
 */
struct Vec2 {
  /**
   * Constructs a Vec2 set to (0, 0).
   */
  constexpr Vec2() : x(0), y(0) {
  }

  /**
   * Constructs a Vec2 with the specified x and y values.
   */
  constexpr Vec2(float x, float y) : x(x), y(y) {
  }

  /**
   * Constructs a Vec2 by loading two floats from the given pointer.
   */
  static Vec2 Load(const float* ptr) {
    Vec2 v;
    memcpy(&v, ptr, sizeof(Vec2));
    return v;
  }

  /**
   * Returns the negation of the vector, computed as (-x, -y).
   */
  Vec2 operator-() const {
    return {-x, -y};
  }

  /**
   * Returns the sum of this vector and another vector, computed as (x + v.x, y + v.y).
   */
  Vec2 operator+(const Vec2& v) const {
    return {x + v.x, y + v.y};
  }

  /**
   * Returns the difference between this vector and another vector, computed as (x - v.x, y - v.y).
   */
  Vec2 operator-(const Vec2& v) const {
    return {x - v.x, y - v.y};
  }

  /**
   * Returns the component-wise product of this vector and another vector, computed as (x * v.x, y * v.y).
   */
  Vec2 operator*(const Vec2& v) const {
    return {x * v.x, y * v.y};
  }

  /**
   * Returns the product of this vector and a scalar, computed as (v.x * s, v.y * s).
   */
  friend Vec2 operator*(const Vec2& v, float s) {
    return {v.x * s, v.y * s};
  }

  /**
   * Returns the product of a scalar and this vector.
   */
  friend Vec2 operator*(float s, const Vec2& v) {
    return v * s;
  }

  /**
   * Returns the quotient of this vector and a scalar, computed as (v.x / s, v.y / s).
   */
  friend Vec2 operator/(const Vec2& v, float s);

  /**
   * The x component value.
   */
  float x;

  /**
   * The y component value.
   */
  float y;
};

/**
 * Vec3 represents a three-dimensional vector with x, y, and z components.
 */
struct Vec3 {
  /**
   * Constructs a Vec3 set to (0, 0, 0).
   */
  constexpr Vec3() : x(0), y(0), z(0) {
  }

  /**
   * Constructs a Vec3 with the specified x, y, and z values.
   */
  constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {
  }

  /**
   * Returns the dot product of two vectors, computed as (a.x * b.x + a.y * b.y + a.z * b.z).
   */
  static float Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }

  /**
   * Returns the dot product of this vector and another vector.
   */
  float dot(const Vec3& v) const {
    return Dot(*this, v);
  }

  /**
   * Returns the cross product of two vectors, computed as
   * (a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x).
   */
  static Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  }

  /**
   * Returns the cross product of this vector and another vector.
   */
  Vec3 cross(const Vec3& v) const {
    return Cross(*this, v);
  }

  /**
   * Returns the normalized (unit length) version of the given vector.
   */
  static Vec3 Normalize(const Vec3& v) {
    return v * (1.f / v.length());
  }

  /**
   * Returns the normalized (unit length) version of this vector.
   */
  Vec3 normalize() const {
    return Normalize(*this);
  }

  /**
   * Returns the length (magnitude) of the vector.
   */
  float length() const {
    return sqrtf(Dot(*this, *this));
  }

  /**
   * Returns a pointer to the vector's immutable data.
   */
  const float* ptr() const {
    return &x;
  }

  /**
   * Returns true if this vector is equal to another vector.
   */
  bool operator==(const Vec3& v) const;

  /**
   * Returns true if this vector is not equal to another vector.
   */
  bool operator!=(const Vec3& v) const {
    return !(*this == v);
  }

  /**
   * Returns the negation of the vector, computed as (-x, -y, -z).
   */
  Vec3 operator-() const {
    return {-x, -y, -z};
  }

  /**
   * Returns the sum of this vector and another vector, computed as (x + v.x, y + v.y, z + v.z).
   */
  Vec3 operator+(const Vec3& v) const {
    return {x + v.x, y + v.y, z + v.z};
  }

  /**
   * Adds another vector to this vector. Sets this vector to (x + v.x, y + v.y, z + v.z).
   */
  void operator+=(const Vec3& v) {
    *this = *this + v;
  }

  /**
   * Returns the difference between this vector and another vector, computed as
   * (x - v.x, y - v.y, z - v.z).
   */
  Vec3 operator-(const Vec3& v) const {
    return {x - v.x, y - v.y, z - v.z};
  }

  /**
   * Subtracts another vector from this vector. Sets this vector to (x - v.x, y - v.y, z - v.z).
   */
  void operator-=(const Vec3& v) {
    *this = *this - v;
  }

  /**
   * Returns the component-wise product of this vector and another vector, computed as
   * (x * v.x, y * v.y, z * v.z).
   */
  Vec3 operator*(const Vec3& v) const {
    return {x * v.x, y * v.y, z * v.z};
  }

  /**
   * Multiplies this vector component-wise by another vector. Sets this vector to
   * (x * v.x, y * v.y, z * v.z).
   */
  void operator*=(const Vec3& v) {
    *this = *this * v;
  }

  /**
   * Returns the product of a vector and a scalar, computed as (v.x * s, v.y * s, v.z * s).
   */
  friend Vec3 operator*(const Vec3& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
  }

  /**
   * Returns the product of a scalar and a vector, computed as (v.x * s, v.y * s, v.z * s).
   */
  friend Vec3 operator*(float s, const Vec3& v) {
    return v * s;
  }

  /**
   * Multiplies this vector by a scalar. Sets this vector to (x * s, y * s, z * s).
   */
  void operator*=(float s) {
    *this = *this * s;
  }

  /**
   * The x component value.
   */
  float x;

  /**
   * The y component value.
   */
  float y;

  /**
   * The z component value.
   */
  float z;
};

/**
 * Vec4 represents a four-dimensional vector with x, y, z, and w components.
 */
struct Vec4 {
  /**
   * Constructs a Vec4 set to (0, 0, 0, 0).
   */
  constexpr Vec4() : x(0), y(0), z(0), w(0) {
  }

  /**
   * Constructs a Vec4 where all components are set to the given value, i.e., (value, value, value, value).
   */
  constexpr Vec4(float value) : x(value), y(value), z(value), w(value) {
  }

  /**
   * Constructs a Vec4 with the specified x, y, z, and w values.
   */
  constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {
  }

  /**
   * Constructs a Vec4 from a Vec3 and a w value, i.e., (v.x, v.y, v.z, w).
   */
  constexpr Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {
  }

  /**
   * Constructs a Vec4 by loading four floats from the given pointer.
   */
  static Vec4 Load(const float* ptr) {
    Vec4 v;
    memcpy(&v, ptr, sizeof(Vec4));
    return v;
  }

  /**
   * Returns the dot product of two vectors, computed as
   * (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w).
   */
  static float Dot(const Vec4& a, const Vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  }

  /**
   * Returns the dot product of this vector and another vector.
   */
  float dot(const Vec4& v) const {
    return Dot(*this, v);
  }

  /**
   * Returns the normalized (unit length) version of the given vector.
   */
  static Vec4 Normalize(const Vec4& v) {
    return v * (1.f / v.length());
  }

  /**
   * Returns the normalized (unit length) version of this vector.
   */
  Vec4 normalize() const {
    return Normalize(*this);
  }

  /**
   * Returns the length (magnitude) of the vector.
   */
  float length() const {
    return sqrtf(Dot(*this, *this));
  }

  /**
   * Returns a pointer to the vector's immutable data.
   */
  const float* ptr() const {
    return &x;
  }

  /**
   * Returns a pointer to the vector's mutable data.
   */
  float* ptr() {
    return &x;
  }

  /**
   * Returns true if this vector is equal to another vector.
   */
  bool operator==(const Vec4& v) const;

  /**
   * Returns true if this vector is not equal to another vector.
   */
  bool operator!=(const Vec4& v) const {
    return !(*this == v);
  }

  /**
   * Returns the negation of the vector, computed as (-x, -y, -z, -w).
   */
  Vec4 operator-() const {
    return {-x, -y, -z, -w};
  }

  /**
   * Returns the sum of this vector and another vector, computed as
   * (x + v.x, y + v.y, z + v.z, w + v.w).
   */
  Vec4 operator+(const Vec4& v) const {
    return {x + v.x, y + v.y, z + v.z, w + v.w};
  }

  /**
   * Adds another vector to this vector. Sets this vector to (x + v.x, y + v.y, z + v.z, w + v.w).
   */
  void operator+=(const Vec4& v) {
    *this = *this + v;
  }

  /**
   * Returns the difference between this vector and another vector, computed as
   * (x - v.x, y - v.y, z - v.z, w - v.w).
   */
  Vec4 operator-(const Vec4& v) const {
    return {x - v.x, y - v.y, z - v.z, w - v.w};
  }

  /**
   * Subtracts another vector from this vector. Sets this vector to (x - v.x, y - v.y, z - v.z, w - v.w).
   */
  void operator-=(const Vec4& v) {
    *this = *this - v;
  }

  /**
   * Returns the component-wise product of this vector and another vector, computed as
   * (x * v.x, y * v.y, z * v.z, w * v.w).
   */
  Vec4 operator*(const Vec4& v) const {
    return {x * v.x, y * v.y, z * v.z, w * v.w};
  }

  /**
   * Multiplies this vector component-wise by another vector. Sets this vector to
   * (x * v.x, y * v.y, z * v.z, w * v.w).
   */
  void operator*=(const Vec4& v) {
    *this = *this * v;
  }

  /**
   * Returns the product of a vector and a scalar, computed as (v.x * s, v.y * s, v.z * s, v.w * s).
   */
  friend Vec4 operator*(const Vec4& v, float s) {
    return {v.x * s, v.y * s, v.z * s, v.w * s};
  }

  /**
   * Returns the product of a scalar and a vector, computed as (v.x * s, v.y * s, v.z * s, v.w * s).
   */
  friend Vec4 operator*(float s, const Vec4& v) {
    return v * s;
  }

  /**
   * Multiplies this vector by a scalar. Sets this vector to (x * s, y * s, z * s, w * s).
   */
  void operator*=(float s) {
    *this = *this * s;
  }

  /**
   * Returns the component-wise quotient of this vector and another vector, computed as
   * (x / v.x, y / v.y, z / v.z, w / v.w).
   */
  Vec4 operator/(const Vec4& v) const {
    return {x / v.x, y / v.y, z / v.z, w / v.w};
  }

  /**
   * Returns the component-wise quotient of a vector and a scalar, computed as
   * (v.x / s, v.y / s, v.z / s, v.w / s).
   */
  friend Vec4 operator/(const Vec4& v, float s) {
    return {v.x / s, v.y / s, v.z / s, v.w / s};
  }

  /**
   * Returns the i-th component of this vector.
   * Valid values for i are 0, 1, 2, and 3. 0 corresponds to x, 1 to y, 2 to z, and 3 to w.
   */
  float operator[](int i) const {
    return this->ptr()[i];
  }

  /**
   * Returns a reference to the i-th component of this vector.
   * Valid values for i are 0, 1, 2, and 3. 0 corresponds to x, 1 to y, 2 to z, and 3 to w.
   */
  float& operator[](int i) {
    return this->ptr()[i];
  }

  /**
   * The x component value.
   */
  float x;

  /**
   * The y component value.
   */
  float y;

  /**
   * The z component value.
   */
  float z;

  /**
   * The w component value.
   */
  float w;
};

/**
 * Shuffles the components of a Vec2 into a Vec4.
 * @tparam Ix Indices specifying which components to use from the Vec2. Valid values for Ix are 0
 * and 1. 0 corresponds to x, and 1 to y.
 */
template <int... Ix>
static inline Vec4 Shuffle(const Vec2& v) {
  const float arr[2] = {v.x, v.y};
  return {arr[Ix]...};
}

/**
 * Returns a vector containing the minimum components of two vectors.
 */
static inline Vec4 Min(const Vec4& a, const Vec4& b) {
  return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z,
          a.w < b.w ? a.w : b.w};
}

/**
 * Returns a vector containing the maximum components of two vectors.
 */
static inline Vec4 Max(const Vec4& a, const Vec4& b) {
  return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z,
          a.w > b.w ? a.w : b.w};
}

}  // namespace tgfx