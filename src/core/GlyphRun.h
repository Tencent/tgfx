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

namespace tgfx {

/**
 * Returns the number of float scalars per glyph for the given positioning mode.
 */
unsigned ScalarsPerGlyph(GlyphPositionMode mode);

/**
 * Computes the transformation matrix for a glyph at the given index within a GlyphRun.
 */
Matrix ComputeGlyphMatrix(const GlyphRun& run, size_t index);

/**
 * Computes the transformation matrix for a glyph at the given index within a GlyphRun.
 * This version reuses an existing Matrix object for better performance in loops.
 */
void ComputeGlyphMatrix(const GlyphRun& run, size_t index, Matrix* matrix);

/**
 * Maps a glyph's bounds by applying the positioning transformation at the given index.
 */
Rect MapGlyphBounds(const GlyphRun& run, size_t index, const Rect& bounds);

/**
 * Returns true if the GlyphRun has complex per-glyph transforms (RSXform or Matrix positioning).
 */
bool HasComplexTransform(const GlyphRun& run);

}  // namespace tgfx
