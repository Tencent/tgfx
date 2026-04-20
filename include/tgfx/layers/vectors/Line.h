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
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Line represents a straight line segment defined by two endpoints.
 */
class Line : public VectorElement {
 public:
  /**
   * Creates a new Line instance.
   */
  static std::shared_ptr<Line> Make();

  /**
   * Returns the start point of the line segment.
   */
  const Point& startPoint() const {
    return _startPoint;
  }

  /**
   * Sets the start point of the line segment.
   */
  void setStartPoint(const Point& value);

  /**
   * Returns the end point of the line segment.
   */
  const Point& endPoint() const {
    return _endPoint;
  }

  /**
   * Sets the end point of the line segment.
   */
  void setEndPoint(const Point& value);

  /**
   * Returns whether the line direction is reversed (from end point to start point).
   */
  bool reversed() const {
    return _reversed;
  }

  /**
   * Sets whether the line direction is reversed.
   */
  void setReversed(bool value);

 protected:
  Type type() const override {
    return Type::Line;
  }

  void apply(VectorContext* context) override;

 protected:
  Line() = default;

 private:
  Point _startPoint = Point::Zero();
  Point _endPoint = {100.0f, 0.0f};
  bool _reversed = false;
  std::shared_ptr<Shape> _cachedShape = nullptr;
};

}  // namespace tgfx
