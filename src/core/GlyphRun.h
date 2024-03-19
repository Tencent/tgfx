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
#include "tgfx/core/Stroke.h"

namespace tgfx {
/**
 * A GlyphRun represents a sequence of glyphs from a single font, along with their positions.
 */
class GlyphRun {
 public:
  /**
   * Constructs an empty GlyphRun.
   */
  GlyphRun() = default;

  /**
   * Constructs a GlyphRun from a font, a sequence of glyph IDs, and a sequence of positions.
   */
  GlyphRun(Font font, std::vector<GlyphID> glyphIDs, std::vector<Point> positions);

  bool empty() const {
    return _glyphIDs.empty();
  }

  /**
   * Returns the font used to render the glyphs in this run.
   */
  const Font& font() const {
    return _font;
  }

  /**
   * Returns true if the font has color.
   */
  bool hasColor() const {
    return _font.getTypeface()->hasColor();
  }

  /**
   * Returns the sequence of glyph IDs in this run.
   */
  const std::vector<GlyphID>& glyphIDs() const {
    return _glyphIDs;
  }

  /**
   * Returns the number of glyphs in this run.
   */
  size_t runSize() const {
    return _glyphIDs.size();
  }

  /**
   * Returns the sequence of positions for the glyphs in this run.
   */
  const std::vector<Point>& positions() const {
    return _positions;
  }

  /**
   * Returns the bounding box of the glyphs in this run.
   */
  Rect getBounds(float scale = 1.0f, const Stroke* stroke = nullptr) const;

  /**
   * Creates a path corresponding to the glyphs in this run. Returns false if failed and ignores the
   * path parameter.
   */
  bool getPath(Path* path, float scale = 1.0f, const Stroke* stroke = nullptr) const;

 private:
  Font _font = {};
  std::vector<GlyphID> _glyphIDs = {};
  std::vector<Point> _positions = {};
};
}  // namespace tgfx
