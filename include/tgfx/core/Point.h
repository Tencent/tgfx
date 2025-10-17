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
   * Constructs a Point set to (0, 0).
   */
  constexpr Point() : x(0), y(0) {
  }

  /**
   * Constructs a Point set to (x, y).
   */
  constexpr Point(float x, float y) : x(x), y(y) {
  }

  /**
   * x-axis value.
   */
  float x;
  /**
   * y-axis value.
   */
  float y;

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

  /**
   * Returns the dot product of a and b.
   */
  static float DotProduct(const Point& a, const Point& b) {
    return (a.x * b.x) + (a.y * b.y);
  }
};
}  // namespace tgfx
