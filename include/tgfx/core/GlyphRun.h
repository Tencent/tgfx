/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
 * Defines the positioning mode of glyphs within a GlyphRun.
 */
enum class GlyphPositioning {
  /**
   * Default positioning: no position data, glyphs are positioned by their advances.
   * Position is computed as (x + accumulated_advances, y) where x and y come from offsetY storage.
   */
  Default = 0,
  /**
   * Horizontal positioning: each glyph has an x position, sharing a common y from offsetY.
   */
  Horizontal = 1,
  /**
   * Point positioning: each glyph has an independent (x, y) position.
   */
  Point = 2,
  /**
   * RSXform positioning: each glyph has rotation, scale, and translation (scos, ssin, tx, ty).
   */
  RSXform = 3,
  /**
   * Matrix positioning: each glyph has a full 2x3 affine matrix.
   */
  Matrix = 4,
};

/**
 * GlyphRun represents a sequence of glyphs sharing the same font and positioning mode. It provides
 * read-only access to glyph data stored in a TextBlob.
 */
struct GlyphRun {
  /**
   * The font used for this run.
   */
  Font font = {};

  /**
   * The number of glyphs in this run.
   */
  size_t glyphCount = 0;

  /**
   * Pointer to the glyph ID array. The array contains glyphCount elements.
   */
  const GlyphID* glyphs = nullptr;

  /**
   * The positioning mode for this run.
   */
  GlyphPositioning positioning = GlyphPositioning::Point;

  /**
   * Pointer to the raw position data array. The number of floats per glyph depends on
   * the positioning mode: Default=0, Horizontal=1, Point=2, RSXform=4, Matrix=6.
   * For Default positioning, this pointer is not used.
   */
  const float* positions = nullptr;

  /**
   * For Default positioning: the shared (x, y) offset for all glyphs.
   * For Horizontal positioning: the shared y offset for all glyphs (x stored in positions).
   * For other modes, this value is 0.
   */
  float offsetY = 0;

  /**
   * For Default positioning: the shared x offset for all glyphs.
   * Only used when positioning is Default.
   */
  float offsetX = 0;
};

}  // namespace tgfx
