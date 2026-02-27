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
#include "tgfx/core/GlyphRun.h"
#include "tgfx/core/RSXform.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
class TextBlob;
struct RunRecord;

/**
 * TextBlobBuilder is used to construct TextBlob objects with efficient memory layout.
 * It allocates contiguous memory for glyph runs and allows zero-copy filling of glyph data.
 */
class TextBlobBuilder {
 public:
  /**
   * RunBuffer provides pointers to the allocated storage for a single run.
   * The caller fills in the glyph IDs and positions directly into these buffers.
   */
  struct RunBuffer {
    /**
     * Storage for glyph IDs. The caller must fill in glyphCount glyph IDs.
     */
    GlyphID* glyphs = nullptr;

    /**
     * Storage for position data. The number of floats to fill depends on the positioning mode:
     * - Default: nullptr (no position data)
     * - Horizontal: glyphCount floats (one x per glyph)
     * - Point: glyphCount * 2 floats (x, y per glyph)
     * - RSXform: glyphCount * 4 floats (scos, ssin, tx, ty per glyph)
     * - Matrix: glyphCount * 6 floats (full affine matrix per glyph)
     * Cast to Point*, RSXform*, or Matrix* as appropriate for the positioning mode.
     */
    float* positions = nullptr;
  };

  TextBlobBuilder();
  ~TextBlobBuilder();

  TextBlobBuilder(const TextBlobBuilder&) = delete;
  TextBlobBuilder& operator=(const TextBlobBuilder&) = delete;

  /**
   * Allocates a run with default positioning. Glyphs are positioned based on their default
   * advances starting from the specified (x, y) position.
   * @param font The font for this run.
   * @param glyphCount Number of glyphs in this run.
   * @param x The x position for the first glyph.
   * @param y The y position for all glyphs.
   * @return RunBuffer with pointer to glyph storage (positions is nullptr).
   */
  const RunBuffer& allocRun(const Font& font, size_t glyphCount, float x, float y);

  /**
   * Allocates a run with horizontal positioning. Each glyph has an x position, and all glyphs
   * share the same y offset.
   * @param font The font for this run.
   * @param glyphCount Number of glyphs in this run.
   * @param y Shared y position for all glyphs.
   * @return RunBuffer with pointers to glyph and x-position storage.
   */
  const RunBuffer& allocRunPosH(const Font& font, size_t glyphCount, float y);

  /**
   * Allocates a run with point positioning. Each glyph has an independent (x, y) position.
   * @param font The font for this run.
   * @param glyphCount Number of glyphs in this run.
   * @return RunBuffer with pointers to glyph and position storage.
   */
  const RunBuffer& allocRunPos(const Font& font, size_t glyphCount);

  /**
   * Allocates a run with RSXform positioning. Each glyph has a rotation, scale, and translation.
   * @param font The font for this run.
   * @param glyphCount Number of glyphs in this run.
   * @return RunBuffer with pointers to glyph and RSXform storage.
   */
  const RunBuffer& allocRunRSXform(const Font& font, size_t glyphCount);

  /**
   * Allocates a run with full matrix positioning. Each glyph has a complete 2D affine transform.
   * @param font The font for this run.
   * @param glyphCount Number of glyphs in this run.
   * @return RunBuffer with pointers to glyph and matrix storage.
   */
  const RunBuffer& allocRunMatrix(const Font& font, size_t glyphCount);

  /**
   * Sets the bounding box for the TextBlob being built. If set, this bounds will be used directly
   * instead of computing it from the glyph data. This is an optimization for callers who already
   * know the bounds. The bounds should be conservative (contain all glyphs).
   * @param bounds The bounding box to use.
   */
  void setBounds(const Rect& bounds);

  /**
   * Builds and returns the TextBlob. After calling this method, the builder is reset and can be
   * reused to build another TextBlob.
   * @return The constructed TextBlob, or nullptr if no runs were added or all runs were empty.
   */
  std::shared_ptr<TextBlob> build();

 private:
  const RunBuffer& allocRunInternal(const Font& font, size_t glyphCount,
                                    GlyphPositioning positioning, Point offset);
  void reserve(size_t size);
  bool tryMerge(const Font& font, GlyphPositioning positioning, size_t count, Point offset);
  void reset();
  void freeStorage();
  RunRecord* lastRun();

  uint8_t* storage = nullptr;
  size_t storageSize = 0;
  size_t storageUsed = 0;
  size_t runCount = 0;
  size_t lastRunOffset = 0;
  RunBuffer currentBuffer = {};
  Rect* userBounds = nullptr;
};
}  // namespace tgfx
