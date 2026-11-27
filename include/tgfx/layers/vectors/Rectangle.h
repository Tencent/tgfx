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

#include <array>
#include "tgfx/core/Point.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Size.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Rectangle represents a rectangle shape with optional rounded corners.
 */
class Rectangle : public VectorElement {
 public:
  /**
   * Creates a new Rectangle instance.
   */
  static std::shared_ptr<Rectangle> Make();

  /**
   * Returns the position of the rectangle center point.
   */
  const Point& position() const {
    return _position;
  }

  /**
   * Sets the position of the rectangle center point.
   */
  void setPosition(const Point& value);

  /**
   * Returns the size of the rectangle.
   */
  const Size& size() const {
    return _size;
  }

  /**
   * Sets the size of the rectangle.
   */
  void setSize(const Size& value);

  /**
   * Returns the per-corner roundness in the order [top-left, top-right, bottom-right, bottom-left].
   * A value of 0 means a sharp corner.
   */
  const std::array<float, 4>& roundness() const {
    return _roundness;
  }

  /**
   * Sets the same roundness for all four corners. Negative values are treated as 0. If the value
   * exceeds half of the shorter rectangle edge, it is proportionally reduced so the rounded
   * corners fit within the rectangle.
   * @param value  roundness applied uniformly to all corners
   */
  void setRoundness(float value);

  /**
   * Sets the per-corner roundness. Negative values are treated as 0. If the sum of two adjacent
   * radii along a shared edge exceeds that edge length, both are proportionally reduced so the
   * rounded corners fit within the rectangle.
   * @param values  per-corner roundness in the order [top-left, top-right, bottom-right,
   *                bottom-left]
   */
  void setRoundness(const std::array<float, 4>& values);

  /**
   * Returns the corner smoothness shared by all four corners.
   */
  float smoothness() const {
    return _smoothness;
  }

  /**
   * Sets the corner smoothness shared by all four corners.
   * Negative values are treated as 0 and values greater than 1 are treated as 1. A value of 0
   * produces a circular corner arc. Larger values make the curvature change more gradually, and
   * the points where each corner meets the edges move away from the corner vertex, to (1 +
   * smoothness) times the roundness. When an edge is too short to fit the smoothed corners at
   * both of its ends, the smoothness is reduced.
   * @param value corner smoothness applied uniformly to all corners
   */
  void setSmoothness(float value);

  /**
   * Returns whether the path direction is reversed (counter-clockwise).
   */
  bool reversed() const {
    return _reversed;
  }

  /**
   * Sets whether the path direction is reversed.
   */
  void setReversed(bool value);

 protected:
  Type type() const override {
    return Type::Rectangle;
  }

  void apply(VectorContext* context) override;

 protected:
  Rectangle() = default;

 private:
  Point _position = Point::Zero();
  Size _size = {100.0f, 100.0f};
  std::array<float, 4> _roundness = {};
  float _smoothness = 0.0f;
  bool _reversed = false;
  std::shared_ptr<Shape> _cachedShape = nullptr;
};

}  // namespace tgfx
