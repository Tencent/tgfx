/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
 * DropShadowStyle adds a shadow that falls behind the layer.
 */
class DropShadowStyle : public LayerStyle {
 public:
  /**
   * Create a layerStyle that draws a drop shadow.
   */
  static std::shared_ptr<DropShadowStyle> Make(float offsetX, float offsetY, float blurrinessX,
                                               float blurrinessY, const Color& color,
                                               bool showBehindTransparent,
                                               BlendMode blendMode = BlendMode::SrcOver);

  /**
   * The x offset of the shadow.
   */
  float offsetX() const {
    return _offsetX;
  }

  /**
   * Set x offset of the shadow.
   * @param offsetX
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
   * @param offsetY
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
   * @param blurrinessY
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
   * @param color
   */
  void setColor(const Color& color);

  /**
   * Whether the shadow should be shown behind transparent pixels.
   */
  bool showBehindTransparent() const {
    return _showBehindTransparent;
  }

  /**
   * Set whether the shadow should be shown behind transparent pixels.
   * @param showBehindTransparent
   */
  void setShowBehindTransparent(bool showBehindTransparent);

  void apply(Canvas* canvas, std::shared_ptr<Image> content, float contentScale,
             float alpha) override;

  Rect filterBounds(const Rect& srcRect, float contentScale) override;

  LayerStylePosition position() const override {
    return LayerStylePosition::Blow;
  }

 private:
  DropShadowStyle(float offsetX, float offsetY, float blurrinessX, float blurrinessY,
                  const Color& color, bool showBehindTransparent, BlendMode blendMode);

  void invalidateFilter();

  std::shared_ptr<ImageFilter> getShadowFilter(float contentScale);

  float _offsetX = 0.0f;
  float _offsetY = 0.0f;
  float _blurrinessX = 0.0f;
  float _blurrinessY = 0.0f;
  Color _color = Color::Black();
  bool _showBehindTransparent = false;

  float currentScale = 1.0f;
  std::shared_ptr<ImageFilter> shadowFilter = nullptr;
};
}  // namespace tgfx
