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

#include "tgfx/core/Canvas.h"
#include "tgfx/layers/LayerProperty.h"
#include "tgfx/layers/layerstyles/LayerStyleInput.h"

namespace tgfx {

/**
 * The position of the layerStyle.
 */
enum class LayerStylePosition {
  /**
   * The layerStyle is drawn above the layer content.
   */
  Above,
  /**
   * The layerStyle is drawn below the layer content.
   */
  Below
};

enum class LayerStyleExtraSourceType {
  /**
   * The layerStyle requires no extra source.
   */
  None,
  /**
   * The layerStyle requires the layer contour to be drawn.
   */
  Contour,
  /**
   * The layerStyle requires the background content.
   */
  Background
};

enum class LayerStyleType {
  LayerStyle,
  BackgroundBlur,
  DropShadow,
  InnerShadow,
  MonoNoise,
  DuoNoise,
  MultiNoise,
  Glass
};

/**
 * LayerStyle is used to change the appearance of a layer. Unlike LayerFilter, it does not create a
 * new offscreen image to replace the original layer content. Instead, it adds visual elements
 * either below or above the layer content, blending directly with all existing content on the
 * canvas. LayerStyles are mutable and can be changed at any time.
 *
 * Layer styles may use the following source images:
 *
 * 1. **Content** (opaque layer content): Renders geometries with normal fills, then converts all
 *    semi-transparent pixels to fully opaque (fully transparent pixels are preserved).
 *
 * 2. **Contour** (layer contour): Similar to Content, but with two differences:
 *    - Includes geometries from painters with alpha=0
 *    - Replaces gradient fills with solid color fills (ignoring local transparency in gradients)
 *
 * 3. **Background** (layer background): The normally rendered content below the current layer.
 */
class LayerStyle : public LayerProperty {
 public:
  virtual LayerStyleType Type() const {
    return LayerStyleType::LayerStyle;
  };

  /**
   * The blend mode used to composite the layer style with the existing content on the canvas.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

  /**
   * Set the blend mode of the layer style.
   */
  void setBlendMode(BlendMode blendMode);

  /**
   * Returns the position of the layerStyle, either above or below the layer content.
   */
  virtual LayerStylePosition position() const = 0;

  /**
   * Returns the bounds of the layer style after applying it to the scaled layer bounds.
   * @param srcRect The scaled bounds of the layer content.
   * @param contentScale The scale factor of the layer bounds relative to its original size.
   * Some layerStyles have size-related parameters that must be adjusted with this scale factor.
   * @return The bounds of the layer style.
   */
  virtual Rect filterBounds(const Rect& srcRect, float contentScale) = 0;

  /**
   * Returns the bounds of the background content after applying the layer style.
   * @param srcRect The scaled bounds of the background content.
   * @param contentScale The scale factor of the background bounds relative to its original size.
   * Some layerStyles have size-related parameters that must be adjusted with this scale factor.
   * @return The bounds of the background content.
   */
  virtual Rect filterBackground(const Rect& srcRect, float contentScale);

  /**
   * Whether to exclude child effects when generating the source images for this layer style.
   * If true, child layer styles and filters are not included in the content, contour, or
   * background images used by this layer style. This option only affects the source images of
   * this layer style, not the layer itself. The default value is false.
   */
  bool excludeChildEffects() const {
    return _excludeChildEffects;
  }

  /**
   * Sets whether to exclude child effects when generating the source images for this layer style.
   */
  void setExcludeChildEffects(bool value);

  /**
   * Returns the type of the extra source required by the layer style.
   * Default is LayerStyleExtraSourceType::None.
   */
  virtual LayerStyleExtraSourceType extraSourceType() const {
    return LayerStyleExtraSourceType::None;
  }

  /**
   * Whether this style also needs the contour image in addition to whatever extraSourceType()
   * requests. Allows Background-sourced styles to access the contour via
   * LayerStyleInput::contourSource. Default is false. When extraSourceType() is already Contour,
   * the contour is delivered via extraSource and this method has no effect. Currently, only
   * GlassStyle overrides this to return true.
   */
  virtual bool needsContour() const {
    return false;
  }

  /**
   * Draws this layer style onto the canvas using the provided input data.
   * @param canvas The canvas to draw the layer style on.
   * @param input The input data for this layer style.
   * @param alpha The alpha transparency value used for drawing the layer style.
   */
  void draw(Canvas* canvas, const LayerStyleInput& input, float alpha) {
    onDraw(canvas, input, alpha, _blendMode);
  }

 protected:
  virtual void onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
                      BlendMode blendMode) = 0;

 private:
  BlendMode _blendMode = BlendMode::SrcOver;
  bool _excludeChildEffects = false;
};
}  // namespace tgfx
