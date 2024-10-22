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
class DropShadowLayerFilter : public LayerFilter {
 public:
  DropShadowLayerFilter() = default;
  virtual ~DropShadowLayerFilter() = default;

  /**
   * Create a filter that draws a drop shadow under the input content.
   */
  static std::shared_ptr<DropShadowLayerFilter> Make();

  /**
   * Set x offset of the shadow.
   * @param dx
   */
  void setDx(float dx);

  /**
   * The x offset of the shadow.
   */
  float dx() const {
    return _dx;
  }

  /**
   * Set y offset of the shadow.
   * @param dy
   */
  void setDy(float dy);

  /**
   * The y offset of the shadow.
   */
  float dy() const {
    return _dy;
  }

  /**
   * Set blur radius for the shadow, along the X axis.
   * @param blurrinessX
   */
  void setBlurrinessX(float blurrinessX);

  /**
   * The blur radius for the shadow, along the X axis.
   */
  float blurrinessX() const {
    return _blurrinessX;
  }

  /**
   * Set blur radius for the shadow, along the Y axis.
   * @param blurrinessY
   */
  void setBlurrinessY(float blurrinessY);

  /**
   * The blur radius for the shadow, along the Y axis.
   */
  float blurrinessY() const {
    return _blurrinessY;
  }

  /**
   * Set the color of the shadow.
   * @param color
   */
  void setColor(const Color& color);

  /**
   * The color of the shadow.
   */
  Color color() const {
    return _color;
  }

  /**
   * Set whether the resulting image does not include the input content.
   */
  void setDropsShadowOnly(bool value);

  /**
   * Whether the resulting image does not include the input content.
   */
  bool dropsShadowOnly() const {
    return _dropsShadowOnly;
  }

 protected:
  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

 private:
  float _dx = 0.0f;
  float _dy = 0.0f;
  float _blurrinessX = 0.0f;
  float _blurrinessY = 0.0f;
  Color _color = Color::Black();
  bool _dropsShadowOnly = false;
};

}  // namespace tgfx
