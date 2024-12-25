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

#include "tgfx/core/Canvas.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {

/**
 * The position of the layerStyle.
 * Above means the layerStyle is drawn above the layer content.
 * Blow means the layerStyle is drawn below the layer content.
 */
enum class LayerStylePosition { Above, Blow };

/**
 * LayerStyle provides a way to draw a layer with a specific style.
 */
class LayerStyle : public LayerProperty {
 public:
  virtual ~LayerStyle() = default;

  /**
   * Apply the layerStyle to the canvas.
   * @param contentScale The scale factor of the source Image relative to its original size.
   * Some layerStyles have size-related parameters that must be adjusted with this scale factor.
   * @param alpha
   */
  virtual void apply(Canvas* canvas, std::shared_ptr<Image> content, float contentScale,
                     float alpha) = 0;

  /**
   * Returns the bounds of the layerStyle.
   * @param contentScale The scale factor of the source Image relative to its original size.
   * Some layerStyles have size-related parameters that must be adjusted with this scale factor.
   */
  virtual Rect filterBounds(const Rect& srcRect, float contentScale) = 0;

  /**
   * Returns the position of the layerStyle.
   */
  virtual LayerStylePosition position() const = 0;

  /**
   * The blend mode of the layerStyle.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

  /**
   * Set the blend mode of the layerStyle.
   * @param blendMode
   */
  void setBlendMode(BlendMode blendMode);

 protected:
  explicit LayerStyle(BlendMode blendMode);

  BlendMode _blendMode = BlendMode::SrcOver;
};
}  // namespace tgfx
