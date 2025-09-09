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

namespace tgfx {

struct Vec3 {
  float x, y, z;

  bool operator==(const Vec3& v) const {
    return x == v.x && y == v.y && z == v.z;
  }

  bool operator!=(const Vec3& v) const {
    return !(*this == v);
  }

  static float Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }

  static Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  }

  static Vec3 Normalize(const Vec3& v) {
    return v * (1.0f / v.length());
  }

  Vec3 operator-() const {
    return {-x, -y, -z};
  }

  Vec3 operator+(const Vec3& v) const {
    return {x + v.x, y + v.y, z + v.z};
  }

  Vec3 operator-(const Vec3& v) const {
    return {x - v.x, y - v.y, z - v.z};
  }

  Vec3 operator*(const Vec3& v) const {
    return {x * v.x, y * v.y, z * v.z};
  }

  friend Vec3 operator*(const Vec3& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
  }

  friend Vec3 operator*(float s, const Vec3& v) {
    return v * s;
  }

  void operator+=(Vec3 v) {
    *this = *this + v;
  }

  void operator-=(Vec3 v) {
    *this = *this - v;
  }

  void operator*=(Vec3 v) {
    *this = *this * v;
  }

  void operator*=(float s) {
    *this = *this * s;
  }

  float lengthSquared() const {
    return Dot(*this, *this);
  }

  float length() const {
    return sqrtf(Dot(*this, *this));
  }

  float dot(const Vec3& v) const {
    return Dot(*this, v);
  }

  Vec3 cross(const Vec3& v) const {
    return Cross(*this, v);
  }

  Vec3 normalize() const {
    return Normalize(*this);
  }

  const float* ptr() const {
    return &x;
  }

  float* ptr() {
    return &x;
  }
};

struct Vec4 {
  float x, y, z, w;

  bool operator==(const Vec4& v) const {
    return x == v.x && y == v.y && z == v.z && w == v.w;
  }

  bool operator!=(const Vec4& v) const {
    return !(*this == v);
  }

  static float Dot(const Vec4& a, const Vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  }

  static Vec4 Normalize(const Vec4& v) {
    return v * (1.0f / v.length());
  }

  Vec4 operator-() const {
    return {-x, -y, -z, -w};
  }

  Vec4 operator+(const Vec4& v) const {
    return {x + v.x, y + v.y, z + v.z, w + v.w};
  }

  Vec4 operator-(const Vec4& v) const {
    return {x - v.x, y - v.y, z - v.z, w - v.w};
  }

  Vec4 operator*(const Vec4& v) const {
    return {x * v.x, y * v.y, z * v.z, w * v.w};
  }

  friend Vec4 operator*(const Vec4& v, float s) {
    return {v.x * s, v.y * s, v.z * s, v.w * s};
  }

  friend Vec4 operator*(float s, const Vec4& v) {
    return v * s;
  }

  float lengthSquared() const {
    return Dot(*this, *this);
  }

  float length() const {
    return sqrtf(Dot(*this, *this));
  }

  float dot(const Vec4& v) const {
    return Dot(*this, v);
  }

  Vec4 normalize() const {
    return Normalize(*this);
  }

  const float* ptr() const {
    return &x;
  }

  float* ptr() {
    return &x;
  }

  float operator[](int i) const {
    return this->ptr()[i];
  }

  float& operator[](int i) {
    return this->ptr()[i];
  }
};

}  // namespace tgfx