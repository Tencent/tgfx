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

#include <memory>
#include <optional>
#include <vector>
#include "tgfx/core/Color.h"
#include "tgfx/core/Point.h"
#include "tgfx/layers/vectors/TextSelector.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * TextModifier applies per-character transform and paint properties to accumulated glyphs in the
 * VectorContext. It uses selectors to determine which characters are affected and by how much.
 */
class TextModifier : public VectorElement {
 public:
  /**
   * Returns the selectors that define which characters this modifier applies to.
   */
  const std::vector<std::shared_ptr<TextSelector>>& selectors() const {
    return _selectors;
  }

  /**
   * Sets the selectors that define which characters this modifier applies to.
   */
  void setSelectors(std::vector<std::shared_ptr<TextSelector>> value);

  // ==================== Transform Properties ====================

  /**
   * Returns the anchor point offset for the selected characters. This affects the center of
   * rotation and scale. The default anchor point for each character is at (advance * 0.5, 0),
   * which is the horizontal center of the character at the baseline. This offset is relative to
   * that default position. Default is (0, 0).
   */
  Point anchorPoint() const {
    return _anchorPoint;
  }

  /**
   * Sets the anchor point offset for the selected characters.
   */
  void setAnchorPoint(Point value);

  /**
   * Returns the position offset for the selected characters. Default is (0, 0).
   */
  Point position() const {
    return _position;
  }

  /**
   * Sets the position offset for the selected characters.
   */
  void setPosition(Point value);

  /**
   * Returns the scale factor for the selected characters. Default is (1.0, 1.0) meaning 100%.
   */
  Point scale() const {
    return _scale;
  }

  /**
   * Sets the scale factor for the selected characters.
   */
  void setScale(Point value);

  /**
   * Returns the skew angle in degrees for the selected characters. Default is 0.
   */
  float skew() const {
    return _skew;
  }

  /**
   * Sets the skew angle in degrees for the selected characters.
   */
  void setSkew(float value);

  /**
   * Returns the skew axis angle in degrees for the selected characters. Default is 0.
   */
  float skewAxis() const {
    return _skewAxis;
  }

  /**
   * Sets the skew axis angle in degrees for the selected characters.
   */
  void setSkewAxis(float value);

  /**
   * Returns the rotation angle in degrees for the selected characters. Default is 0.
   */
  float rotation() const {
    return _rotation;
  }

  /**
   * Sets the rotation angle in degrees for the selected characters.
   */
  void setRotation(float value);

  /**
   * Returns the alpha multiplier for the selected characters. Default is 1.0 (fully opaque).
   */
  float alpha() const {
    return _alpha;
  }

  /**
   * Sets the alpha multiplier for the selected characters.
   */
  void setAlpha(float value);

  // ==================== Paint Properties ====================

  /**
   * Returns the fill color override for the selected characters. When set, it overrides the
   * FillStyle color.
   */
  std::optional<Color> fillColor() const {
    return _fillColor;
  }

  /**
   * Sets the fill color override for the selected characters. Pass std::nullopt to clear.
   */
  void setFillColor(std::optional<Color> value);

  /**
   * Returns the stroke color override for the selected characters. When set, it overrides the
   * StrokeStyle color.
   */
  std::optional<Color> strokeColor() const {
    return _strokeColor;
  }

  /**
   * Sets the stroke color override for the selected characters. Pass std::nullopt to clear.
   */
  void setStrokeColor(std::optional<Color> value);

  /**
   * Returns the stroke width override for the selected characters. When set, it overrides the
   * StrokeStyle strokeWidth.
   */
  std::optional<float> strokeWidth() const {
    return _strokeWidth;
  }

  /**
   * Sets the stroke width override for the selected characters. Pass std::nullopt to clear.
   */
  void setStrokeWidth(std::optional<float> value);

 protected:
  Type type() const override {
    return Type::TextModifier;
  }

  void attachToLayer(Layer* layer) override;

  void detachFromLayer(Layer* layer) override;

  void apply(VectorContext* context) override;

 private:
  std::vector<std::shared_ptr<TextSelector>> _selectors = {};

  // Transform properties
  Point _anchorPoint = Point::Zero();
  Point _position = Point::Zero();
  Point _scale = Point::Make(1.0f, 1.0f);
  float _skew = 0.0f;
  float _skewAxis = 0.0f;
  float _rotation = 0.0f;
  float _alpha = 1.0f;

  // Paint properties
  std::optional<Color> _fillColor = {};
  std::optional<Color> _strokeColor = {};
  std::optional<float> _strokeWidth = {};
};

}  // namespace tgfx
