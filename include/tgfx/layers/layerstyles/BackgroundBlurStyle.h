/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
 * BackgroundBlurStyle adds a blur effect to the background of the layer.
 * The background includes all layers below this layer and the layer's below layerStyles except for
 * the BackgroundBlurStyle.
 */
class BackgroundBlurStyle : public LayerStyle {
 public:
  static std::shared_ptr<BackgroundBlurStyle> Make(float blurrinessX, float blurrinessY,
                                                   TileMode tileMode = TileMode::Mirror);

  LayerStyleType Type() const override {
    return LayerStyleType::BackgroundBlur;
  }

  /**
   * The x blurriness of the background.
   */
  float blurrinessX() const {
    return _blurrinessX;
  }

  /**
   * Set x blurriness of the background.
   */
  void setBlurrinessX(float blurriness);

  /**
   * The y blurriness of the background.
   */
  float blurrinessY() const {
    return _blurrinessY;
  }

  /**
   * Set y blurriness of the background.
   */
  void setBlurrinessY(float blurriness);

  /**
   * The tile mode of the background.
   */
  TileMode tileMode() const {
    return _tileMode;
  }

  /**
   * Set tile mode of the background.
   */
  void setTileMode(TileMode tileMode);

  LayerStylePosition position() const override {
    return LayerStylePosition::Below;
  }

  Rect filterBounds(const Rect& srcRect, float) override {
    return srcRect;
  }

  Rect filterBackground(const Rect& srcRect, float contentScale) override;

  LayerStyleExtraSourceType extraSourceType() const override {
    return _blurrinessX > 0 && _blurrinessY > 0 ? LayerStyleExtraSourceType::Background
                                                : LayerStyleExtraSourceType::None;
  }

 protected:
  void onDraw(Canvas*, std::shared_ptr<Image>, float, float, BlendMode) override {
  }

  void onDrawWithExtraSource(Canvas* canvas, std::shared_ptr<Image> contour, float contentScale,
                             std::shared_ptr<Image> extraSource, const Point& extraSourceOffset,
                             float alpha, BlendMode blendMode) override;

 private:
  BackgroundBlurStyle(float blurrinessX, float blurrinessY, TileMode tileMode);

  std::shared_ptr<ImageFilter> getBackgroundFilter(float contentScale);

  float _blurrinessX = 0;
  float _blurrinessY = 0;
  TileMode _tileMode = TileMode::Mirror;
  std::shared_ptr<ImageFilter> backgroundFilter = nullptr;
  float currentScale = 0.0f;
};

}  // namespace tgfx
