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
  Rectangle() = default;

  /**
   * Returns the center point of the rectangle.
   */
  const Point& center() const {
    return _center;
  }

  /**
   * Sets the center point of the rectangle.
   */
  void setCenter(const Point& value);

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
   * Returns the corner roundness. A value of 0 means sharp corners.
   */
  float roundness() const {
    return _roundness;
  }

  /**
   * Sets the corner roundness.
   */
  void setRoundness(float value);

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

 private:
  Point _center = Point::Zero();
  Size _size = {};
  float _roundness = 0.0f;
  bool _reversed = false;
  std::shared_ptr<Shape> _cachedShape = nullptr;
};

}  // namespace tgfx
