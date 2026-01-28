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

#include "tgfx/core/GlyphRun.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

/**
 * Returns the number of float scalars per glyph for the given positioning mode.
 */
unsigned ScalarsPerGlyph(GlyphPositioning positioning);

/**
 * Returns the transformation matrix for a glyph at the given index within a GlyphRun.
 * This version reuses an existing Matrix object for better performance in loops.
 */
inline void GetGlyphMatrix(const GlyphRun& run, size_t index, Matrix* matrix) {
  switch (run.positioning) {
    case GlyphPositioning::Horizontal:
      matrix->setTranslate(run.positions[index], run.offsetY);
      break;
    case GlyphPositioning::Point: {
      auto* points = reinterpret_cast<const Point*>(run.positions);
      matrix->setTranslate(points[index].x, points[index].y);
      break;
    }
    case GlyphPositioning::RSXform: {
      const float* p = run.positions + index * 4;
      float scos = p[0];
      float ssin = p[1];
      matrix->setAll(scos, -ssin, p[2], ssin, scos, p[3]);
      break;
    }
    case GlyphPositioning::Matrix: {
      const float* p = run.positions + index * 6;
      matrix->setAll(p[0], p[1], p[2], p[3], p[4], p[5]);
      break;
    }
  }
}

/**
 * Returns the transformation matrix for a glyph at the given index within a GlyphRun.
 */
inline Matrix GetGlyphMatrix(const GlyphRun& run, size_t index) {
  Matrix matrix = {};
  GetGlyphMatrix(run, index, &matrix);
  return matrix;
}

/**
 * Maps a glyph's bounds by applying the positioning transformation at the given index.
 */
Rect MapGlyphBounds(const GlyphRun& run, size_t index, const Rect& bounds);

/**
 * Returns true if the GlyphRun has complex per-glyph transforms (RSXform or Matrix positioning).
 */
inline bool HasComplexTransform(const GlyphRun& run) {
  return run.positioning == GlyphPositioning::RSXform ||
         run.positioning == GlyphPositioning::Matrix;
}

}  // namespace tgfx
