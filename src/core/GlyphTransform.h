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
 * Returns the transformation matrix for a glyph at the given index within a GlyphRun.
 */
Matrix GetGlyphMatrix(const GlyphRun& run, size_t index);

/**
 * Returns the position of a glyph at the given index within a GlyphRun. Only valid for Horizontal
 * and Point positioning modes.
 */
inline Point GetGlyphPosition(const GlyphRun& run, size_t index) {
  if (run.positioning == GlyphPositioning::Horizontal) {
    return {run.positions[index], run.offsetY};
  }
  return reinterpret_cast<const Point*>(run.positions)[index];
}

/**
 * Returns true if the GlyphRun has complex per-glyph transforms (RSXform or Matrix positioning).
 */
inline bool HasComplexTransform(const GlyphRun& run) {
  return run.positioning == GlyphPositioning::RSXform ||
         run.positioning == GlyphPositioning::Matrix;
}

}  // namespace tgfx
