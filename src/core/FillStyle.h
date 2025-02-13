/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
 * FillStyle specifies how the geometry of a drawing operation is filled.
 */
class FillStyle {
 public:
  /**
   * Returns true if pixels on the active edges of Path may be drawn with partial transparency. The
   * default value is true.
   */
  bool antiAlias = true;

  /**
   * The input color, unpremultiplied, as four floating point values.
   */
  Color color = Color::White();

  /**
   * Optional colors used when filling a geometry if set, such as a gradient.
   */
  std::shared_ptr<Shader> shader = nullptr;

  /**
   * Optional mask filter used to modify the alpha channel of the fill when drawing.
   */
  std::shared_ptr<MaskFilter> maskFilter = nullptr;

  /**
   * Optional color filter used to modify the color of the fill when drawing.
   */
  std::shared_ptr<ColorFilter> colorFilter = nullptr;

  /**
   * The blend mode used to combine the fill with the destination pixels.
   */
  BlendMode blendMode = BlendMode::SrcOver;

  /**
   * Returns true if the FillStyle contains only a color and no shader, mask filter, or color filter.
   */
  bool hasOnlyColor() const;

  /**
   * Returns true if the FillStyle is guaranteed to produce only opaque colors.
   */
  bool isOpaque() const;

  /**
   * Returns true if the FillStyle is equal to the given style. If ignoreColor is true, the color
   * is not compared.
   */
  bool isEqual(const FillStyle& style, bool ignoreColor = false) const;

  /**
   * Returns a new FillStyle applying the given matrix to the shader and mask filter.
   */
  FillStyle makeWithMatrix(const Matrix& matrix) const;
};
}  // namespace tgfx
