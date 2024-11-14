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
   * Returns the bounding box of the glyphs in this run. The resolutionScale parameter is used to
   * scale the glyphs before measuring. However, the resolutionScale is not applied to the returned
   * bounds; it just affects the precision of the bounds.
   */
  Rect getBounds(float resolutionScale = 1.0f) const;

  /**
   * Creates a path corresponding to the glyphs in this run. The resolutionScale parameter is used
   * to scale the glyphs before creating the path. However, the resolutionScale is not applied to
   * the returned path; it just affects the precision of the path. Returns true if the path was
   * successfully created. Otherwise, returns false and leaves the path unchanged.
   */
  bool getPath(Path* path, float resolutionScale = 1.0f) const;

 private:
  std::vector<GlyphRun> _glyphRuns = {};
};
}  // namespace tgfx
