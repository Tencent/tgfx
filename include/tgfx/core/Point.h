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

#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace tgfx {
/**
 * Point holds two 32-bit floating point coordinates.
 */
struct Point {
  /**
   * x-axis value.
   */
  float x;
  /**
   * y-axis value.
   */
  float y;

  /**
   * Creates a Point set to (0, 0).
   */
  static const Point& Zero() {
    static const Point zero = Point::Make(0, 0);
    return zero;
  }

  /**
   * Creates a Point with specified x and y value.
   */
  static constexpr Point Make(float x, float y) {
    return {x, y};
  }

  /**
   * Creates a Point with specified x and y value.
   */
  static constexpr Point Make(int x, int y) {
    return {static_cast<float>(x), static_cast<float>(y)};
  }

  /**
   * Returns true if x and y are both zero.
   */
  bool isZero() const {
    return (0 == x) && (0 == y);
  }

  /**
   * Sets x to xValue and y to yValue.
   */
  void set(float xValue, float yValue) {
    x = xValue;
    y = yValue;
  }

  /**
   * Adds offset (dx, dy) to Point.
   */
  void offset(float dx, float dy) {
    x += dx;
    y += dy;
  }

  /**
   * Returns the Euclidean distance from origin.
   */
  float length() const {
    return Point::Length(x, y);
  }

  /**
   * Returns true if a is equivalent to b.
   */
  friend bool operator==(const Point& a, const Point& b) {
    return a.x == b.x && a.y == b.y;
  }

  /**
   * Returns true if a is not equivalent to b.
   */
  friend bool operator!=(const Point& a, const Point& b) {
    return a.x != b.x || a.y != b.y;
  }

  /**
   * Returns a Point from b to a; computed as (a.x - b.x, a.y - b.y).
   */
  friend Point operator-(const Point& a, const Point& b) {
    return {a.x - b.x, a.y - b.y};
  }

  /** 
    * Subtracts vector Point v from Point. Sets Point to: (x - v.x, y - v.y).
    */
  void operator-=(const Point& v) {
    x -= v.x;
    y -= v.y;
  }

  /**
   * Returns Point resulting from Point a offset by Point b, computed as:
   * (a.x + b.x, a.y + b.y).
   */
  friend Point operator+(const Point& a, const Point& b) {
    return {a.x + b.x, a.y + b.y};
  }

  /** 
    * offset vector point v from Point. Sets Point to: (x + v.x, y + v.y).
    */
  void operator+=(const Point& v) {
    x += v.x;
    y += v.y;
  }

  /** 
    * Returns Point multiplied by scale.
    * (x * scale, y * scale)
    */
  friend Point operator*(const Point& p, float scale) {
    return {p.x * scale, p.y * scale};
  }

  /** 
    * Multiplies Point by scale. Sets Point to: (x * scale, y * scale).
    */
  void operator*=(float scale) {
    x *= scale;
    y *= scale;
  }

  /**
   * Returns the Euclidean distance from origin.
   */
  static float Length(float x, float y) {
    return sqrt(x * x + y * y);
  }

  /**
   * Returns the Euclidean distance between a and b.
   */
  static float Distance(const Point& a, const Point& b) {
    return Length(a.x - b.x, a.y - b.y);
  }

  /** Returns the cross product of vector a and vector b.

        a and b form three-dimensional vectors with z-axis value equal to zero. The
        cross product is a three-dimensional vector with x-axis and y-axis values equal
        to zero. The cross product z-axis component is returned.

        @param a  left side of cross product
        @param b  right side of cross product
        @return   area spanned by vectors signed by angle direction
    */
  static float CrossProduct(const Point& a, const Point& b) {
    return a.x * b.y - a.y * b.x;
  }

  /** Returns the dot product of vector a and vector b.

        @param a  left side of dot product
        @param b  right side of dot product
        @return   product of input magnitudes and cosine of the angle between them
    */
  static float DotProduct(const Point& a, const Point& b) {
    return a.x * b.x + a.y * b.y;
  }

  bool normalize() {
    if (isZero()) {
      return false;
    }
    double xx = x;
    double yy = y;
    double length = sqrt(xx * xx + yy * yy);
    double scale = 1.0 / length;
    x = static_cast<float>(x * scale);
    y = static_cast<float>(y * scale);
    float prod = 0;
    prod *= x;
    prod *= y;
    if (isZero() || prod != 0) {
      set(0, 0);
      return false;
    }
    return true;
  }
};
}  // namespace tgfx
