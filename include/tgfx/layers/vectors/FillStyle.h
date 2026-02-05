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

#include "tgfx/core/BlendMode.h"
#include "tgfx/layers/LayerPaint.h"
#include "tgfx/layers/vectors/ColorSource.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Defines the rule used to determine the interior of a shape when filling.
 */
enum class FillRule {
  /**
   * Enclosed by a non-zero sum of contour directions.
   */
  Winding,
  /**
   * Enclosed by an odd number of contours.
   */
  EvenOdd
};

/**
 * FillStyle applies a fill to shapes in the same group using a ColorSource.
 */
class FillStyle : public VectorElement {
 public:
  /**
   * Creates a new FillStyle instance with the specified color source.
   * @param colorSource The color source used for the fill. If null, returns nullptr.
   * @return A new FillStyle instance, or nullptr if colorSource is null.
   */
  static std::shared_ptr<FillStyle> Make(std::shared_ptr<ColorSource> colorSource);

  /**
   * Returns the color source used for the fill.
   */
  std::shared_ptr<ColorSource> colorSource() const {
    return _colorSource;
  }

  /**
   * Returns the alpha value applied to the fill. Ranges from 0.0 (fully transparent) to 1.0 (fully
   * opaque). The default value is 1.0.
   */
  float alpha() const {
    return _alpha;
  }

  /**
   * Sets the alpha value applied to the fill.
   */
  void setAlpha(float value);

  /**
   * Returns the blend mode used when drawing the fill. The default value is BlendMode::SrcOver.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

  /**
   * Sets the blend mode used when drawing the fill.
   */
  void setBlendMode(BlendMode value);

  /**
   * Returns the fill rule used to determine the interior of the shape.
   */
  FillRule fillRule() const {
    return _fillRule;
  }

  /**
   * Sets the fill rule used to determine the interior of the shape.
   */
  void setFillRule(FillRule value);

  /**
   * Returns the placement of the fill relative to the layer's children. The default value is
   * LayerPlacement::Background.
   */
  LayerPlacement placement() const {
    return _placement;
  }

  /**
   * Sets the placement of the fill relative to the layer's children.
   */
  void setPlacement(LayerPlacement value);

 protected:
  Type type() const override {
    return Type::FillStyle;
  }

  void attachToLayer(Layer* layer) override;

  void detachFromLayer(Layer* layer) override;

  void apply(VectorContext* context) override;

 protected:
  explicit FillStyle(std::shared_ptr<ColorSource> colorSource)
      : _colorSource(std::move(colorSource)) {
  }

 private:
  std::shared_ptr<ColorSource> _colorSource = nullptr;
  float _alpha = 1.0f;
  BlendMode _blendMode = BlendMode::SrcOver;
  FillRule _fillRule = FillRule::Winding;
  LayerPlacement _placement = LayerPlacement::Background;
};

}  // namespace tgfx
