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

#include "tgfx/core/Font.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class GlyphRun;

/**
 * TextBlob combines multiple glyphs, Font, and positions into an immutable container.
 */
class TextBlob {
 public:
  /**
   * Creates a new TextBlob from the given text. The text must be in utf-8 encoding. This function
   * uses the default character-to-glyph mapping from the Typeface in font. It does not perform
   * typeface fallback for characters not found in the Typeface. Glyphs are positioned based on
   * their default advances.
   */
  static std::shared_ptr<TextBlob> MakeFrom(const std::string& text, const Font& font);

  /**
   * Creates a new TextBlob from the given glyphs, positions, and text font.
   */
  static std::shared_ptr<TextBlob> MakeFrom(const GlyphID glyphIDs[], const Point positions[],
                                            size_t glyphCount, const Font& font);

  virtual ~TextBlob() = default;

  /**
   * Returns the bounding box of the TextBlob when drawn with the given Matrix.
   */
  Rect getBounds(const Matrix& matrix = Matrix::I()) const;

 protected:
  /**
   * Returns the number of glyph runs in the text blob.
   */
  virtual size_t glyphRunCount() const = 0;

  /**
   * Returns the glyph runs at the given index.
   */
  virtual const GlyphRun* getGlyphRun(size_t i) const = 0;

  friend class Mask;
};
}  // namespace tgfx
