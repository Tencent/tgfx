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

#include "tgfx/core/GlyphRun.h"

namespace tgfx {
class GlyphRunList;

/**
 * TextBlob combines multiple text runs into an immutable container. Each text run consists of
 * glyphs, positions, and font.
 */
class TextBlob {
 public:
  /**
   * Creates a new TextBlob from the given text. The text must be in utf-8 encoding. This function
   * uses the default character-to-glyph mapping from the Typeface in font. It does not perform
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

  virtual ~TextBlob() = default;

  /**
   * Returns the bounding box of the TextBlob when drawn with the given Matrix.
   */
  Rect getBounds(const Matrix& matrix = Matrix::I()) const;

 private:
  std::vector<std::shared_ptr<GlyphRunList>> glyphRunLists = {};

  explicit TextBlob(std::vector<std::shared_ptr<GlyphRunList>> runLists)
      : glyphRunLists(std::move(runLists)) {
  }

  friend class Canvas;
  friend class Mask;
  friend class TextContent;
};
}  // namespace tgfx
