/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

namespace tgfx {
/**
 * GlyphRun represents a sequence of glyphs from a single font, along with their positions.
 */
struct GlyphRun {
  /**
   * Constructs an empty GlyphRun.
   */
  GlyphRun() = default;

  /**
   * Constructs a GlyphRun using a font, a list of glyph IDs, and their positions.
   */
  GlyphRun(Font font, std::vector<GlyphID> glyphIDs, std::vector<Point> positions)
      : font(std::move(font)), glyphs(std::move(glyphIDs)), positions(std::move(positions)) {
  }

  /**
   * Returns the Font used to render the glyphs in this run.
   */
  Font font = {};

  /**
   * Returns the sequence of glyph IDs in this run.
   */
  std::vector<GlyphID> glyphs = {};

  /**
   * Returns the sequence of positions for each glyph in this run.
   */
  std::vector<Point> positions = {};
};
}  // namespace tgfx
