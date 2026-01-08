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

#include "tgfx/core/GlyphRun.h"

namespace tgfx {
/**
 * TextBlob combines multiple text runs into an immutable container. Each text run consists of
 * glyphs, positions, and font.
 */
class TextBlob {
 public:
  /**
   * Creates a new TextBlob from the given text. The text must be in utf-8 encoding. This function
   * uses the default character-to-glyph mapping from the Typeface in font. It doesn't perform
   * typeface fallback for characters not found in the Typeface. Glyphs are positioned based on
   * their default advances. Returns nullptr if the text is empty or fails to map any characters to
   * glyphs.
   */
  static std::shared_ptr<TextBlob> MakeFrom(const std::string& text, const Font& font);

  /**
   * Creates a new TextBlob from the given glyphs, positions, and text font. Returns nullptr if the
   * glyphCount is 0.
   */
  static std::shared_ptr<TextBlob> MakeFrom(const GlyphID glyphIDs[], const Point positions[],
                                            size_t glyphCount, const Font& font);

  /**
   * Creates a new TextBlob from a single glyph run. Returns nullptr if the glyph run is empty or
   * has mismatched glyph and position counts.
   */
  static std::shared_ptr<TextBlob> MakeFrom(GlyphRun glyphRun);

  /**
   * Creates a new TextBlob from the given glyph runs. Returns nullptr if glyphRuns is empty or if
   * any of the glyph runs are invalid (i.e., have mismatched glyph and position counts).
   */
  static std::shared_ptr<TextBlob> MakeFrom(std::vector<GlyphRun> glyphRuns);

  ~TextBlob();

  /**
   * Returns a conservative bounding box for the TextBlob that is guaranteed to contain all glyphs.
   * It may be larger than the actual bounds, but it is faster to compute.
   */
  Rect getBounds() const;

  /**
   * Returns the tight bounding box of the TextBlob when drawn with the given Matrix. Because text
   * outlines can vary with different scale factors, it's best to use the final drawing matrix for
   * accurate bounds. This method is more accurate than getBounds, but also more computationally
   * expensive.
   */
  Rect getTightBounds(const Matrix* matrix = nullptr) const;

  /**
   * Tests if the specified point hits any glyph in this TextBlob. Each glyph is tested
   * individually using its actual path for precise hit testing. For color glyphs (e.g., emoji),
   * bounds are used instead since they don't have outlines. If a stroke is provided, it will be
   * applied to the glyph path or bounds before testing.
   */
  bool hitTestPoint(float localX, float localY, const Stroke* stroke = nullptr) const;

  /**
   * Returns the glyph runs that make up this TextBlob.
   */
  const std::vector<GlyphRun>& glyphRuns() const {
    return _glyphRuns;
  }

 private:
  std::vector<GlyphRun> _glyphRuns = {};
  mutable std::atomic<Rect*> bounds = {nullptr};

  explicit TextBlob(std::vector<GlyphRun> glyphRuns) : _glyphRuns(std::move(glyphRuns)) {
  }

  Rect computeBounds() const;
};
}  // namespace tgfx
