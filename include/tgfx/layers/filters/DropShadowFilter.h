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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {
/**
 * A filter draws a drop shadow under the input content.
 */
class DropShadowFilter : public LayerFilter {
 public:
  /**
   * Create a filter that draws a drop shadow under the input content.
   */
  static std::shared_ptr<DropShadowFilter> Make(float offsetX, float offsetY, float blurrinessX,
                                                float blurrinessY, const Color& color,
                                                bool dropsShadowOnly = false);

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
   * Whether the resulting image does not include the input content.
   */
  bool dropsShadowOnly() const {
    return _dropsShadowOnly;
  }

  /**
   * Set whether the resulting image does not include the input content.
   */
  void setDropsShadowOnly(bool value);

 protected:
  LayerFilterType Type() const override {
    return LayerFilterType::DropShadowFilter;
  }
  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

 private:
  DropShadowFilter(float offsetX, float offsetY, float blurrinessX, float blurrinessY,
                   const Color& color, bool dropsShadowOnly);
  float _offsetX = 0.0f;
  float _offsetY = 0.0f;
  float _blurrinessX = 0.0f;
  float _blurrinessY = 0.0f;
  Color _color = Color::Black();
  bool _dropsShadowOnly = false;
  friend class LayerFilterSerialization;
};

}  // namespace tgfx
