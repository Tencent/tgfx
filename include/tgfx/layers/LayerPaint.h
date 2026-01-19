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
   * The placement of the content relative to the layer's children. Default is
   * LayerPlacement::Background.
   */
  LayerPlacement placement = LayerPlacement::Background;
};

}  // namespace tgfx
