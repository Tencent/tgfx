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

#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

/**
 * ShapeStyle defines the visual appearance for filling or stroking shapes in a ShapeLayer. It
 * contains a color, an optional shader for color generation, and a blend mode for compositing.
 * ShapeStyle is immutable; to change properties, create a new instance.
 */
class ShapeStyle {
 public:
  /**
   * Creates a new ShapeStyle with a solid color.
   * @param color The solid color to use.
   * @param blendMode The blend mode for compositing. Default is BlendMode::SrcOver.
   */
  static std::shared_ptr<ShapeStyle> Make(const Color& color,
                                          BlendMode blendMode = BlendMode::SrcOver);

  /**
   * Creates a new ShapeStyle with a shader.
   * @param shader The shader to use for color generation.
   * @param alpha The alpha transparency value (0.0 to 1.0). Default is 1.0.
   * @param blendMode The blend mode for compositing. Default is BlendMode::SrcOver.
   */
  static std::shared_ptr<ShapeStyle> Make(std::shared_ptr<Shader> shader, float alpha = 1.0f,
                                          BlendMode blendMode = BlendMode::SrcOver);

  /**
   * Returns the color used for rendering. If a shader is set, only the alpha component of the color
   * is used. The default value is Color::White().
   */
  Color color() const {
    return _color;
  }

  /**
   * Returns the alpha transparency value. This is equivalent to color().alpha. Valid values are 0
   * (fully transparent) to 1 (fully opaque). The default value is 1.
   */
  float alpha() const {
    return _color.alpha;
  }

  /**
   * Returns the optional shader used for color generation. If nullptr, the color is used directly.
   */
  std::shared_ptr<Shader> shader() const {
    return _shader;
  }

  /**
   * Returns the blend mode used to composite the shape with the content below it. The default value
   * is BlendMode::SrcOver.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

 private:
  ShapeStyle(const Color& color, std::shared_ptr<Shader> shader, BlendMode blendMode)
      : _color(color), _shader(std::move(shader)), _blendMode(blendMode) {
  }

  Color _color = Color::White();
  std::shared_ptr<Shader> _shader = nullptr;
  BlendMode _blendMode = BlendMode::SrcOver;
};

}  // namespace tgfx
