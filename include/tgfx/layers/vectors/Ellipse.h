/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Ellipse represents an ellipse shape.
 */
class Ellipse : public VectorElement {
 public:
  Ellipse() = default;

  /**
   * Returns the position of the ellipse center.
   */
  const Point& position() const {
    return _position;
  }

  /**
   * Sets the position of the ellipse center.
   */
  void setPosition(const Point& value);

  /**
   * Returns the size of the ellipse (width and height of the bounding box).
   */
  const Point& size() const {
    return _size;
  }

  /**
   * Sets the size of the ellipse.
   */
  void setSize(const Point& value);

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
    return Type::Ellipse;
  }

  void apply(VectorContext* context) override;

 private:
  Point _position = Point::Zero();
  Point _size = Point::Zero();
  bool _reversed = false;
  std::shared_ptr<Shape> _cachedShape = nullptr;
};

}  // namespace tgfx
