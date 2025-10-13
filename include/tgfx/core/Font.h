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

#include "tgfx/core/FontMetrics.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
 * Font controls options applied when drawing and measuring text.
 */
class Font {
 public:
  /**
   * Constructs Font with default values.
   */
  Font();

  /**
   * Constructs Font with default values with Typeface and size in points.
   */
  Font(std::shared_ptr<Typeface> typeface, float size);

  /**
   * Returns a new font with the same attributes of this font, but with the specified size.
   */
  Font makeWithSize(float size) const;

  /**
   * Returns a typeface reference if set, nullptr otherwise.
   */
  std::shared_ptr<Typeface> getTypeface() const;

  /**
   * Returns true if the font has color glyphs, for example, color emojis.
   */
  bool hasColor() const;

  /**
   * Returns true if the font has outline glyphs, meaning it can generate paths.
   */
  bool hasOutlines() const;

  /**
   * Sets a new Typeface to this Font.
   */
  void setTypeface(std::shared_ptr<Typeface> newTypeface);

  /**
   * Returns the point size of this font.
   */
  float getSize() const;

  /**
   * Sets text size in points. Has no effect if textSize is not greater than or equal to zero.
   */
  void setSize(float newSize);

  /**
   * Returns true if bold is approximated by increasing the stroke width when drawing glyphs.
   */
  bool isFauxBold() const {
    return fauxBold;
  }

  /**
   * Increases stroke width when drawing glyphs to approximate a bold typeface.
   */
  void setFauxBold(bool value) {
    fauxBold = value;
  }

  /**
   * Returns true if italic is approximated by adding skewX value of a canvas's matrix when
   * drawing glyphs.
   */
  bool isFauxItalic() const {
    return fauxItalic;
  }

  /**
   * Adds skewX value of a canvas's matrix when drawing glyphs to approximate a italic typeface.
   */
  void setFauxItalic(bool value) {
    fauxItalic = value;
  }

  /**
   * Returns the glyph ID corresponds to the specified glyph name. The glyph name must be in utf-8
   * encoding. Returns 0 if the glyph name is not in this Font.
   */
  GlyphID getGlyphID(const std::string& name) const;

  /**
   * Returns the glyph ID corresponds to the specified unicode code point. Returns 0 if the code
   * point is not in this Font.
   */
  GlyphID getGlyphID(Unichar unichar) const;

  /**
   * Returns the FontMetrics associated with this font. Results are scaled by text size but do not
   * take into account dimensions required by fauxBold and fauxItalic.
   */
  FontMetrics getMetrics() const;

  /**
   * Returns the bounding box of the specified glyph.
   */
  Rect getBounds(GlyphID glyphID) const;

  /**
   * Returns the advance for specified glyph.
   * @param glyphID The id of specified glyph.
   * @param verticalText The intended drawing orientation of the glyph. Please note that it is not
   * supported on the web platform.
   */
  float getAdvance(GlyphID glyphID, bool verticalText = false) const;

  /**
   * Calculates the offset from the default (horizontal) origin to the vertical origin for specified
   * glyph.
   */
  Point getVerticalOffset(GlyphID glyphID) const;

  /**
   * Creates a Path corresponding to glyph outline. If glyph has an outline, copies outline to the
   * path and returns true. If glyph is described by a bitmap, returns false and ignores the path.
   */
  bool getPath(GlyphID glyphID, Path* path) const;

  /**
   * Creates an Image capturing the content of the specified glyph. The returned matrix should apply
   * to the glyph image when drawing. Returns nullptr if the glyph is not part of this Font,
   * cannot be rendered as an image, or if the stroke is unsupported.
   */
  std::shared_ptr<ImageCodec> getImage(GlyphID glyphID, const Stroke* stroke, Matrix* matrix) const;

  /**
   * Compares two fonts for equality.
   */
  bool operator==(const Font& font) const;

  /**
   * Compares two fonts for inequality.
   */
  bool operator!=(const Font& font) const {
    return !(*this == font);
  }

 private:
  std::shared_ptr<ScalerContext> scalerContext = nullptr;
  bool fauxBold = false;
  bool fauxItalic = false;

  friend class RenderContext;
};
}  // namespace tgfx
