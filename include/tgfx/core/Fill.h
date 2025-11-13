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
 * Fill specifies how the geometry of a drawing operation is filled.
 */
class Fill {
 public:
  /**
   * Constructs a Fill with default values.
   */
  Fill() = default;

  /**
   * Constructs a Fill with the specified color, blend mode, and antialiasing.
   */
  Fill(const Color& color, BlendMode blendMode, bool antiAlias = true)
      : color(color), blendMode(blendMode), antiAlias(antiAlias) {
  }

  /**
   * The input color, which is unpremultiplied, may exceed the 0-1 range. The
   * default value is opaque white.
   */
  Color color = {};

  /**
   * The blend mode used to combine the fill with the destination pixels.
   */
  BlendMode blendMode = BlendMode::SrcOver;

  /**
   * Returns true if pixels on the active edges of Path may be drawn with partial transparency. The
   * default value is true.
   */
  bool antiAlias = true;

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
   * Returns true if the Fill is guaranteed to produce only opaque colors.
   */
  bool isOpaque() const;

  /**
   * Returns true if the Paint prevents any drawing.
   */
  bool nothingToDraw() const;

  /**
   * Returns a new Fill applying the given matrix to the shader and mask filter.
   */
  Fill makeWithMatrix(const Matrix& matrix) const;
};
}  // namespace tgfx
