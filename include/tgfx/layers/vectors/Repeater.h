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

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Defines how copies are composited in a repeater.
 */
enum class RepeaterOrder {
  /**
   * Copies are drawn from bottom to top.
   */
  BelowOriginal,
  /**
   * Copies are drawn from top to bottom.
   */
  AboveOriginal
};

/**
 * Repeater duplicates shapes in the same group with transformation applied to each copy.
 */
class Repeater : public VectorElement {
 public:
  Repeater() = default;

  /**
   * Returns the number of copies to create.
   */
  float copies() const {
    return _copies;
  }

  /**
   * Sets the number of copies to create.
   */
  void setCopies(float value);

  /**
   * Returns the offset of the first copy.
   */
  float offset() const {
    return _offset;
  }

  /**
   * Sets the offset of the first copy.
   */
  void setOffset(float value);

  /**
   * Returns the compositing order of the copies.
   */
  RepeaterOrder order() const {
    return _order;
  }

  /**
   * Sets the compositing order of the copies.
   */
  void setOrder(RepeaterOrder value);

  /**
   * Returns the anchor point for transformation.
   */
  const Point& anchorPoint() const {
    return _anchorPoint;
  }

  /**
   * Sets the anchor point for transformation.
   */
  void setAnchorPoint(const Point& value);

  /**
   * Returns the position offset applied to each copy.
   */
  const Point& position() const {
    return _position;
  }

  /**
   * Sets the position offset applied to each copy.
   */
  void setPosition(const Point& value);

  /**
   * Returns the rotation in degrees applied to each copy.
   */
  float rotation() const {
    return _rotation;
  }

  /**
   * Sets the rotation in degrees applied to each copy.
   */
  void setRotation(float value);

  /**
   * Returns the scale applied to each copy. {1.0, 1.0} means 100%.
   */
  const Point& scale() const {
    return _scale;
  }

  /**
   * Sets the scale applied to each copy.
   */
  void setScale(const Point& value);

  /**
   * Returns the starting alpha for the first copy (0.0 to 1.0).
   */
  float startAlpha() const {
    return _startAlpha;
  }

  /**
   * Sets the starting alpha for the first copy.
   */
  void setStartAlpha(float value);

  /**
   * Returns the ending alpha for the last copy (0.0 to 1.0).
   */
  float endAlpha() const {
    return _endAlpha;
  }

  /**
   * Sets the ending alpha for the last copy.
   */
  void setEndAlpha(float value);

 protected:
  Type type() const override {
    return Type::Repeater;
  }

  void apply(VectorContext* context) override;

 private:
  Matrix getMatrix(float progress) const;

  float _copies = 1.0f;
  float _offset = 0.0f;
  RepeaterOrder _order = RepeaterOrder::BelowOriginal;
  Point _anchorPoint = Point::Zero();
  Point _position = Point::Zero();
  float _rotation = 0.0f;
  Point _scale = {1.0f, 1.0f};
  float _startAlpha = 1.0f;
  float _endAlpha = 1.0f;
};

}  // namespace tgfx
