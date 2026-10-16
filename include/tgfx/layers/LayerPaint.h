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

#include "tgfx/core/Paint.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/layers/StrokeAlign.h"

namespace tgfx {

/**
 * Defines the placement of layer content relative to its children.
 */
enum class LayerPlacement {
  /**
   * Place the content behind the layer's children (default).
   */
  Background,
  /**
   * Place the content in front of the layer's children.
   */
  Foreground
};

/**
 * LayerPaint defines the visual style for rendering layer content. It is a simplified version of
 * Paint, containing only the properties relevant to layer content rendering.
 */
class LayerPaint {
 public:
  /**
   * Creates an empty LayerPaint with default values.
   */
  LayerPaint() = default;

  /**
   * Creates a LayerPaint with the specified color and blend mode.
   * @param color The color to use for rendering.
   * @param blendMode The blend mode used to composite the content with the background.
   */
  explicit LayerPaint(const Color& color, BlendMode blendMode = BlendMode::SrcOver)
      : color(color), blendMode(blendMode) {
  }

  /**
   * Creates a LayerPaint with the specified shader, alpha and blend mode.
   * @param shader The shader used to generate colors.
   * @param alpha The alpha value applied to the shader.
   * @param blendMode The blend mode used to composite the content with the background.
   */
  explicit LayerPaint(std::shared_ptr<Shader> shader, float alpha = 1.0f,
                      BlendMode blendMode = BlendMode::SrcOver)
      : shader(std::move(shader)), blendMode(blendMode) {
    color.alpha = alpha;
  }

  /**
   * The color used when rendering. Default is Color::White().
   */
  Color color = Color::White();

  /**
   * Optional shader used to generate colors, such as gradients or image patterns. Default is
   * nullptr.
   */
  std::shared_ptr<Shader> shader = nullptr;

  /**
   * The blend mode used to composite the content with the background. Default is
   * BlendMode::SrcOver.
   */
  BlendMode blendMode = BlendMode::SrcOver;

  /**
   * Whether the geometry is filled or stroked. Default is PaintStyle::Fill.
   */
  PaintStyle style = PaintStyle::Fill;

  /**
   * The stroke options if the style is set to PaintStyle::Stroke. Default is empty (width=0).
   */
  Stroke stroke = {};

  /**
   * The alignment of the stroke relative to the geometry boundary. Only meaningful when style is
   * PaintStyle::Stroke. Default is StrokeAlign::Center.
   */
  StrokeAlign strokeAlign = StrokeAlign::Center;

  /**
   * Optional path effect (e.g. dash) applied to the geometry before stroking. Only meaningful when
   * style is PaintStyle::Stroke. Default is nullptr.
   */
  std::shared_ptr<PathEffect> pathEffect = nullptr;

  /**
   * The placement of the content relative to the layer's children. Default is
   * LayerPlacement::Background.
   */
  LayerPlacement placement = LayerPlacement::Background;

  /**
   * When true, this paint only takes part in the layer's own silhouette: it is left out of the
   * contour shared with layer styles (shadows, background blur) and out of the shape ordinary
   * styles see, and only the layer's own contour recording includes it as a contour. Normal drawing
   * is untouched, so a paint that also carries a visible alpha is still drawn. Default is false.
   *
   * It exists because alpha cannot express the difference: a paint with a fully transparent color
   * is indistinguishable from a fully transparent user fill, while a caller may need geometry that
   * still defines the layer's outline. The flag only selects which contour and which shape consume
   * the paint; whether it is drawn is still decided by the paint's own alpha, so a caller that
   * needs no visible color must also set the alpha to 0. Skipping the draw here would instead break
   * a caller that later makes the paint opaque.
   */
  bool silhouetteOnly = false;
};

}  // namespace tgfx
