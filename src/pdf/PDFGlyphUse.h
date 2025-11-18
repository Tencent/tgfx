/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <cstddef>
#include "../../include/tgfx/core/Log.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
class PDFGlyphUse {
 public:
  PDFGlyphUse() : bitset(0) {
  }

  PDFGlyphUse(GlyphID firstNonZero, GlyphID lastGlyph)
      : bitset(lastGlyph - firstNonZero + 2), _firstNonZero(firstNonZero), _lastGlyph(lastGlyph) {
    DEBUG_ASSERT(firstNonZero >= 1);
  }

  ~PDFGlyphUse() = default;
  PDFGlyphUse(PDFGlyphUse&&) = default;
  PDFGlyphUse& operator=(PDFGlyphUse&&) = default;
  PDFGlyphUse(const PDFGlyphUse&) = delete;
  PDFGlyphUse& operator=(const PDFGlyphUse&) = delete;

  GlyphID firstNonZero() const {
    return _firstNonZero;
  }

  GlyphID lastGlyph() const {
    return _lastGlyph;
  }

  void set(GlyphID glyphID) {
    bitset[toCode(glyphID)] = true;
  }

  bool has(GlyphID glyphID) const {
    return bitset[toCode(glyphID)];
  }

  template <typename CALLBACK>
  void getSetValues(CALLBACK func) const {
    if (_firstNonZero == 1) {
      for (size_t i = 0; i < bitset.size(); ++i) {
        if (bitset[i]) {
          func(i);
        }
      }
      return;
    }
    uint16_t offset = _firstNonZero - 1;
    for (size_t i = 0; i < bitset.size(); ++i) {
      if (bitset[i]) {
        func(i == 0 ? i : i + offset);
      }
    }
  }

 private:
  std::vector<bool> bitset;
  GlyphID _firstNonZero = 0;
  GlyphID _lastGlyph = 0;

  uint16_t toCode(GlyphID glyphID) const {
    if (glyphID == 0 || _firstNonZero == 1) {
      return glyphID;
    }
    DEBUG_ASSERT((glyphID >= _firstNonZero && glyphID <= _lastGlyph));
    return glyphID - _firstNonZero + 1;
  }
};
}  // namespace tgfx