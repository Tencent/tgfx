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
 * DropShadowStyle adds a shadow below the layer.
 */
class DropShadowStyle : public LayerStyle {
 public:
  /**
   * Create a layerStyle that adds a shadow below the layer.
   */
  static std::shared_ptr<DropShadowStyle> Make(float offsetX, float offsetY, float blurrinessX,
                                               float blurrinessY, const Color& color,
                                               bool showBehindLayer = true);

  LayerStyleType Type() const override {
    return LayerStyleType::DropShadow;
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
   * @param blurrinessX
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
   * Whether the shadow behind the layer content should be visible. The default value is true.
   */
  bool showBehindLayer() const {
    return _showBehindLayer;
  }

  /**
   * Set whether the shadow behind the layer content should be visible.
   */
  void setShowBehindLayer(bool showBehindLayer);

  LayerStylePosition position() const override {
    return LayerStylePosition::Below;
  }

  Rect filterBounds(const Rect& srcRect, float contentScale) override;

  LayerStyleExtraSourceType extraSourceType() const override {
    return !_showBehindLayer ? LayerStyleExtraSourceType::Contour : LayerStyleExtraSourceType::None;
  }

 protected:
  void onDraw(Canvas* canvas, std::shared_ptr<Image> contour, float contentScale, float alpha,
              BlendMode blendMode) override;

  void onDrawWithExtraSource(Canvas* canvas, std::shared_ptr<Image> contour, float contentScale,
                             std::shared_ptr<Image> extraSource, const Point& extraSourceOffset,
                             float alpha, BlendMode blendMode) override;

 private:
  DropShadowStyle(float offsetX, float offsetY, float blurrinessX, float blurrinessY,
                  const Color& color, bool showBehindTransparent);

  void invalidateFilter();

  std::shared_ptr<ImageFilter> getShadowFilter(float contentScale);

  float _offsetX = 0.0f;
  float _offsetY = 0.0f;
  float _blurrinessX = 0.0f;
  float _blurrinessY = 0.0f;
  Color _color = Color::Black();
  bool _showBehindLayer = true;

  float currentScale = 1.0f;
  std::shared_ptr<ImageFilter> shadowFilter = nullptr;

  friend class Layer;
};
}  // namespace tgfx
