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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/RSXform.h"

namespace tgfx {

struct RunRecord;

/**
 * Describes the positioning mode of glyphs within a GlyphRun.
 */
enum class GlyphPositioning {
  /**
   * Horizontal positioning: each glyph has an x position, sharing a common y from offsetY().
   */
  Horizontal = 0,
  /**
   * Point positioning: each glyph has an independent (x, y) position.
   */
  Point = 1,
  /**
   * RSXform positioning: each glyph has rotation, scale, and translation (scos, ssin, tx, ty).
   */
  RSXform = 2,
  /**
   * Matrix positioning: each glyph has a full 2x3 affine matrix.
   */
  Matrix = 3,
};

/**
 * Returns the number of float scalars per glyph for the given positioning mode.
 */
unsigned ScalarsPerGlyph(GlyphPositioning positioning);

/**
 * GlyphRun represents a sequence of glyphs sharing the same font and positioning mode. It provides
 * read-only access to glyph data stored in a TextBlob.
 */
class GlyphRun {
 public:
  /**
   * Returns the number of glyphs in this run.
   */
  size_t glyphCount() const;

  /**
   * Returns a pointer to the glyph ID array. The array contains glyphCount() elements.
   */
  const GlyphID* glyphs() const;

  /**
   * Returns the font for this run.
   */
  const Font& font() const;

  /**
   * Returns the positioning mode for this run.
   */
  GlyphPositioning positioning() const;

  /**
   * Returns a pointer to the raw position data array. The number of floats per glyph depends on
   * the positioning mode: Horizontal=1, Point=2, RSXform=4, Matrix=6.
   */
  const float* positions() const;

  /**
   * Returns the shared y offset for Horizontal positioning mode. For other positioning modes,
   * returns 0.
   */
  float offsetY() const;

  /**
   * Returns the position data as Point array. Only valid when positioning() == Point.
   */
  const Point* points() const;

  /**
   * Returns the position data as RSXform array. Only valid when positioning() == RSXform.
   */
  const RSXform* xforms() const;

  /**
   * Returns the position data as Matrix array. Only valid when positioning() == Matrix.
   */
  const Matrix* matrices() const;

 private:
  const RunRecord* record = nullptr;
  friend class TextBlob;
};

}  // namespace tgfx
