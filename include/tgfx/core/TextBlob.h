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

#include <memory>
#include "tgfx/core/Font.h"
#include "tgfx/core/GlyphRun.h"
#include "tgfx/core/RSXform.h"

namespace tgfx {
class TextBlobBuilder;
struct RunRecord;

/**
 * TextBlob combines multiple text runs into an immutable container. Each text run consists of
 * glyphs, positions, and font. The object and run data are stored in a single contiguous memory
 * block for efficiency.
 *
 * Example usage for iterating over glyph runs:
 *   for (auto run : *blob) {
 *       const Font& font = run.font;
 *       for (size_t i = 0; i < run.glyphCount; ++i) {
 *           GlyphID glyph = run.glyphs[i];
 *           // Access position data based on run.positioning
 *       }
 *   }
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
   * Creates a new TextBlob with horizontal positioning. Each glyph has an x position, and all
   * glyphs share the same y offset. Returns nullptr if the glyphCount is 0.
   * @param glyphIDs Array of glyph IDs.
   * @param xPositions Array of x positions, one per glyph.
   * @param glyphCount Number of glyphs.
   * @param y Shared y position for all glyphs.
   * @param font The font for rendering.
   */
  static std::shared_ptr<TextBlob> MakeFromPosH(const GlyphID glyphIDs[], const float xPositions[],
                                                size_t glyphCount, float y, const Font& font);

  /**
   * Creates a new TextBlob with RSXform positioning. Each glyph has a rotation, scale, and
   * translation. Returns nullptr if the glyphCount is 0.
   * @param glyphIDs Array of glyph IDs.
   * @param xforms Array of RSXform values, one per glyph.
   * @param glyphCount Number of glyphs.
   * @param font The font for rendering.
   */
  static std::shared_ptr<TextBlob> MakeFromRSXform(const GlyphID glyphIDs[], const RSXform xforms[],
                                                   size_t glyphCount, const Font& font);

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

  class Iterator {
   public:
    ~Iterator();

    GlyphRun operator*() const;

    Iterator& operator++();

    bool operator!=(const Iterator& other) const {
      return remaining != other.remaining;
    }

   private:
    Iterator(const RunRecord* record, size_t remaining, float* positions);

    const RunRecord* current = nullptr;
    size_t remaining = 0;
    float* expandedPositions = nullptr;
    float* currentPositions = nullptr;
    friend class TextBlob;
  };

  Iterator begin() const;

  Iterator end() const {
    return Iterator(nullptr, 0, nullptr);
  }

 private:
  size_t runCount = 0;
  mutable std::atomic<Rect*> bounds = {nullptr};

  explicit TextBlob(size_t runCount);
  TextBlob(size_t runCount, const Rect& bounds);

  void operator delete(void* p);
  void* operator new(size_t, void* p);

  const RunRecord* firstRun() const;
  Rect computeBounds() const;

  friend class TextBlobBuilder;
};
}  // namespace tgfx
