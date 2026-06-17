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

#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

/**
 * InnerShadowStyle adds an inner shadow above the layer.
 */
class InnerShadowStyle : public LayerStyle {
 public:
  /**
   * Creates a layer style that adds an inner shadow above the layer.
   */
  static std::shared_ptr<InnerShadowStyle> Make(float offsetX, float offsetY, float blurrinessX,
                                                float blurrinessY, const Color& color);

  LayerStyleType Type() const override {
    return LayerStyleType::InnerShadow;
  }

  /**
   * The x offset of the shadow.
   */
  float offsetX() const {
    return _offsetX;
  }

  /**
   * Set x offset of the shadow.
   */
  void setOffsetX(float offsetX);

  /**
   * The y offset of the shadow.
   */
  float offsetY() const {
    return _offsetY;
  }

  /**
   * Set y offset of the shadow.
   */
  void setOffsetY(float offsetY);

  /**
   * The blur radius for the shadow, along the X axis.
   */
  float blurrinessX() const {
    return _blurrinessX;
  }

  /**
   * Set blur radius for the shadow, along the X axis.
   */
  void setBlurrinessX(float blurrinessX);

  /**
   * The blur radius for the shadow, along the Y axis.
   */
  float blurrinessY() const {
    return _blurrinessY;
  }

  /**
   * Set blur radius for the shadow, along the Y axis.
   */
  void setBlurrinessY(float blurrinessY);

  /**
   * The color of the shadow.
   */
  Color color() const {
    return _color;
  }

  /**
   * Set the color of the shadow.
   */
  void setColor(const Color& color);

  /**
   * Controls how much the inner shadow expands (positive) or shrinks (negative).
   * When zero (the default), the inner shadow is rendered directly from the layer's rasterized
   * content image without any spread adjustment.
   * When positive, the shadow coverage grows inward, making the shadow thicker. When negative,
   * the shadow coverage shrinks, making the shadow thinner. The spread is derived from the layer's
   * vector shape (e.g. Rect, Oval, or RRect); if the vector shape cannot be extracted, the layer's
   * bounding rect is used as a fallback.
   */
  float spread() const {
    return _spread;
  }

  /**
   * Sets the spread distance for the inner shadow.
   */
  void setSpread(float spread);

  LayerStylePosition position() const override {
    return LayerStylePosition::Above;
  }

  Rect filterBounds(const Rect& srcRect, float contentScale) override;

  LayerStyleExtraSourceType extraSourceType() const override {
    return LayerStyleExtraSourceType::Contour;
  }

  bool needContourShape() const override;

 private:
  InnerShadowStyle(float offsetX, float offsetY, float blurrinessX, float blurrinessY,
                   const Color& color);

  void onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
              BlendMode blendMode) override;

  void drawWithSpread(Canvas* canvas, const LayerStyleInput& input, float alpha,
                      BlendMode blendMode);

  std::shared_ptr<ImageFilter> getShadowFilter(float scale);

  void invalidateFilter();

  float _offsetX = 0.0f;
  float _offsetY = 0.0f;
  float _blurrinessX = 0.0f;
  float _blurrinessY = 0.0f;
  Color _color = Color::Black();
  std::shared_ptr<ImageFilter> shadowFilter = nullptr;
  float currentScale = 0.0f;
  float _spread = 0.0f;
};
}  // namespace tgfx
