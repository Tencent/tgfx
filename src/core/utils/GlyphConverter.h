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

#include <unordered_map>
#include "tgfx/core/Font.h"
#include "tgfx/core/Typeface.h"
namespace tgfx {

#ifdef TGFX_USE_GLYPH_TO_UNICODE

/**
 * The glyph converter can convert glyphs to Unicode characters and cache the mapping of glyphs to
 * Unicode. The cache is released when the converter is destructed.
 */
class GlyphConverter {
 public:
  GlyphConverter() = default;
  ~GlyphConverter() = default;

  std::vector<Unichar> glyphsToUnichars(const Font& font, const std::vector<GlyphID>& glyphs);

 private:
  const std::vector<Unichar>& getGlyphToUnicodeMap(const std::shared_ptr<Typeface>& typeface);

  std::unordered_map<uint32_t, std::vector<Unichar>> fontToGlyphMap;
};

#endif
}  // namespace tgfx
