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

#include "core/GlyphRun.h"

namespace tgfx {

unsigned ScalarsPerGlyph(GlyphPositioning positioning);

// RunRecord stores metadata for a single run in the TextBlob's contiguous memory.
// Memory layout after RunRecord:
//   [glyphs: GlyphID[glyphCount]] (aligned to 4 bytes)
//   [positions: float[glyphCount * scalarsPerGlyph]] (if scalarsPerGlyph > 0)
struct RunRecord {
  Font font;
  GlyphPositioning positioning = GlyphPositioning::Point;
  uint32_t glyphCount = 0;
  float y = 0.0f;  // Only used for Horizontal positioning
  uint8_t flags = 0;

  static constexpr uint8_t LAST_FLAG = 0x01;

  static size_t StorageSize(size_t count, GlyphPositioning pos);

  GlyphID* glyphBuffer();
  const GlyphID* glyphBuffer() const;

  float* posBuffer();
  const float* posBuffer() const;

  size_t storageSize() const;

  bool isLast() const {
    return (flags & LAST_FLAG) != 0;
  }

  void setLast() {
    flags |= LAST_FLAG;
  }

  const RunRecord* next() const;

  void grow(uint32_t count);
};

}  // namespace tgfx
