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

#include "core/utils/LazyBounds.h"
#include "tgfx/core/GlyphRun.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class TextBlob;

/**
 * GlyphRunList contains a list of glyph runs that can be drawn together. All glyph runs in a list
 * share the same font type, whether they have color, outlines, or neither.
 */
class GlyphRunList {
 public:
  /**
   * Unwraps a TextBlob into a list of GlyphRunList objects. Returns nullptr if the TextBlob is
   * nullptr.
   */
  static const std::vector<std::shared_ptr<GlyphRunList>>* Unwrap(const TextBlob* textBlob);

  /**
   * Constructs a GlyphRunList using a single glyph run.
   */
  explicit GlyphRunList(GlyphRun glyphRun);

  /**
   * Constructs a GlyphRunList using a list of glyph runs. All glyph runs in the list must share the
   * same font type, whether they have color, outlines, or neither.
   */
  explicit GlyphRunList(std::vector<GlyphRun> glyphRuns);

  /**
   * Returns true if the GlyphRunList has color.
   */
  bool hasColor() const {
    return _glyphRuns[0].font.hasColor();
  }

  /**
   * Returns true if the GlyphRunList has outlines.
   */
  bool hasOutlines() const {
    return _glyphRuns[0].font.hasOutlines();
  }

  /**
   * Returns the glyph runs in the text blob.
   */
  const std::vector<GlyphRun>& glyphRuns() const {
    return _glyphRuns;
  }

  /**
   * Computes glyph bounds using the font bounds and each glyph's position
   */
  Rect getBounds() const;

  /**
   * Returns the tight bounding box of the glyphs in this run. If a matrix is provided, the bounds
   * will be transformed accordingly. Compared to getBounds, this method is more accurate but also
   * more computationally expensive.
   */
  Rect getTightBounds(const Matrix* matrix = nullptr) const;

  /**
   * Creates a Path for the glyphs in this run. If a matrix is provided, the path will be transformed
   * accordingly. Returns true if the path was successfully created. Otherwise,
   * returns false and leaves the path unchanged.
   */
  bool getPath(Path* path, const Matrix* matrix = nullptr) const;

  /**
   * Tests if the specified point hits any glyph in this run list. Each glyph is tested
   * individually using its actual path for precise hit testing. For color glyphs (e.g., emoji),
   * bounds are used instead since they don't have outlines. If a stroke is provided, it will be
   * applied to the glyph path or bounds before testing.
   */
  bool hitTestPoint(float localX, float localY, const Stroke* stroke = nullptr) const;

 private:
  std::vector<GlyphRun> _glyphRuns = {};
  LazyBounds bounds = {};

  Rect computeConservativeBounds() const;
};
}  // namespace tgfx
