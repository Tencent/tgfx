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

#include <memory>
#include "tgfx/core/Image.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
* GlyphFace is a provider for glyphs. It provides the glyph path, image, and bounds, etc.
*/
class GlyphFace {
 public:
  GlyphFace() = default;
  virtual ~GlyphFace() = default;

  /**
   * Returns true if the font has color glyphs, for example, color emojis.
   */
  virtual bool hasColor() const = 0;

  /**
   * Returns true if the font has outline glyphs, meaning it can generate paths.
   */
  virtual bool hasOutlines() const = 0;

  /**
   * Returns a new GlyphFace with the same attributes of this font, but with the specified scale.
   */
  virtual std::shared_ptr<GlyphFace> makeScaled(float scale) = 0;

  /**
   * Creates a path corresponding to glyph outline. If glyph has an outline, copies outline to path
   * and returns true. If glyph is described by a bitmap, returns false and ignores path parameter.
   */
  virtual bool getPath(GlyphID glyphID, Path* path) const = 0;

  /**
   * Creates an Image capturing the content of the specified glyph. The returned matrix should apply
   * to the glyph image when drawing. Please note that the fauxBold is not supported for this
   * method.
   */
  virtual std::shared_ptr<Image> getImage(GlyphID glyphID, Matrix* matrix) const = 0;

  /**
   * Returns the bounding box of the specified glyph.
   */
  virtual Rect getBounds(GlyphID glyphID) const = 0;

  /**
   * 
   */
  virtual Font asFont() const = 0;
};
}  // namespace tgfx
