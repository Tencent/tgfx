/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Color.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Shader.h"

namespace tgfx {
/**
 * Brush defines the appearance attributes for rendering geometry, including color, shader,
 * blending, and filtering options. It applies to both fill and stroke operations.
 */
class Brush {
 public:
  /**
   * Constructs a Brush with default values.
   */
  Brush() = default;

  /**
   * Constructs a Brush with the specified color, blend mode, and antialiasing.
   */
  Brush(const Color& color, BlendMode blendMode, bool antiAlias = true)
      : color(color), blendMode(blendMode), antiAlias(antiAlias) {
  }

  /**
   * The input color, unpremultiplied, as four floating point values. The default value is opaque
   * white.
   */
  Color color = {};

  /**
   * The blend mode used to combine the brush color with the destination pixels.
   */
  BlendMode blendMode = BlendMode::SrcOver;

  /**
   * Specifies whether pixels on the active edges may be drawn with partial transparency. The
   * default value is true.
   */
  bool antiAlias = true;

  /**
   * Optional shader used to generate colors when rendering, such as gradients or image patterns.
   */
  std::shared_ptr<Shader> shader = nullptr;

  /**
   * Optional mask filter used to modify the alpha channel when drawing.
   */
  std::shared_ptr<MaskFilter> maskFilter = nullptr;

  /**
   * Optional color filter used to modify the color when drawing.
   */
  std::shared_ptr<ColorFilter> colorFilter = nullptr;

  /**
   * Returns true if the Brush is guaranteed to produce only opaque colors.
   */
  bool isOpaque() const;

  /**
   * Returns true if the Brush prevents any drawing.
   */
  bool nothingToDraw() const;

  /**
   * Returns a new Brush with the given matrix applied to the shader and mask filter.
   */
  Brush makeWithMatrix(const Matrix& matrix) const;
};
}  // namespace tgfx
