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

#include <memory>
#include "tgfx/core/Font.h"

namespace tgfx {

struct RunRecord;
class TextBlob;

enum class GlyphPositioning {
  Horizontal = 0,
  Point = 1,
  RSXform = 2,
  Matrix = 3,
};

struct GlyphRun {
  Font font = {};
  GlyphPositioning positioning = GlyphPositioning::Horizontal;
  const GlyphID* glyphs = nullptr;
  const float* positions = nullptr;

  static GlyphRun From(const RunRecord* record);

  size_t runSize() const {
    return _runSize;
  }

  /**
   * Returns true if the glyph run has complex per-glyph transforms (RSXform or Matrix positioning),
   * not just simple translations.
   */
  bool hasComplexTransform() const {
    return positioning == GlyphPositioning::RSXform || positioning == GlyphPositioning::Matrix;
  }

  /**
   * Returns the transformation matrix for the glyph at the given index.
   */
  Matrix getMatrix(size_t index) const;

 private:
  size_t _runSize = 0;
  float offsetY = 0.0f;  // Only used for Horizontal positioning
  friend struct RunRecord;
  friend class RenderContext;
};

}  // namespace tgfx
