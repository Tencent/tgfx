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
#include "tgfx/core/PathTypes.h"
#include "tgfx/layers/LayerPaint.h"
#include "tgfx/layers/vectors/ColorSource.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * FillStyle applies a fill to shapes in the same group using a ColorSource.
 */
class FillStyle : public VectorElement {
 public:
  FillStyle() = default;

  /**
   * Returns the color source used for the fill.
   */
  std::shared_ptr<ColorSource> colorSource() const {
    return _colorSource;
  }

  /**
   * Sets the color source used for the fill.
   */
  void setColorSource(std::shared_ptr<ColorSource> value);

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
  PathFillType fillRule() const {
    return _fillRule;
  }

  /**
   * Sets the fill rule used to determine the interior of the shape.
   */
  void setFillRule(PathFillType value);

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

 private:
  std::shared_ptr<ColorSource> _colorSource = nullptr;
  float _alpha = 1.0f;
  BlendMode _blendMode = BlendMode::SrcOver;
  PathFillType _fillRule = PathFillType::Winding;
  LayerPlacement _placement = LayerPlacement::Background;
};

}  // namespace tgfx
