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
#include "tgfx/core/Font.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
 * GlyphFace is a render-only font that contains only the necessary information to render glyphs.
 * It can be implemented externally to render glyphs from a custom font or used as a wrapper
 * around a Font object. GlyphFace is thread-safe and immutable.
 */
class GlyphFace {
 public:
  /**
   * Wraps an existing Font object in a GlyphFace. Returns nullptr if the Font has no typeface.
   */
  static std::shared_ptr<GlyphFace> Wrap(Font font);

  virtual ~GlyphFace() = default;

  /**
   * Returns true if the glyph face has color glyphs, for example, color emojis.
   */
  virtual bool hasColor() const = 0;

  /**
   * Returns true if the glyph face has outline glyphs, meaning it can generate paths.
   */
  virtual bool hasOutlines() const = 0;

  /**
   * Returns a new GlyphFace with the same attributes as this one, but with the glyph size scaled by
   * the specified factor. If the scale is less than or equal to 0, returns nullptr.
   */
  virtual std::shared_ptr<GlyphFace> makeScaled(float scale) const = 0;

  /**
   * Creates a path corresponding to glyph outline. If glyph has an outline, copies outline to path
   * and returns true. If glyph is described by a bitmap, returns false and ignores path parameter.
   */
  virtual bool getPath(GlyphID glyphID, Path* path) const = 0;

  /**
   * Creates an Image capturing the content of the specified glyph. The returned matrix should apply
   * to the glyph image when drawing. Returns nullptr if the glyph is not part of this GlyphFace,
   * cannot be rendered as an image, or if the stroke is unsupported.
   */
  virtual std::shared_ptr<ImageCodec> getImage(GlyphID glyphID, const Stroke* stroke,
                                               Matrix* matrix) const = 0;

  /**
   * Returns the bounding box of the specified glyph.
   */
  virtual Rect getBounds(GlyphID glyphID) const = 0;

  /**
   * Checks if this GlyphFace is backed by a Font object. If it is, sets the font pointer to the
   * backing Font object and returns true. Otherwise, returns false and leaves the font pointer
   * unchanged.
   */
  virtual bool asFont(Font* font) const = 0;

  /*
   * Returns the scale of this GlyphFace
   */
  virtual float getScale() const = 0;

 protected:
  GlyphFace() = default;
};
}  // namespace tgfx
