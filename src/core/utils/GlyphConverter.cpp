/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GlyphConverter.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {

#ifdef TGFX_USE_GLYPH_TO_UNICODE
std::vector<Unichar> GlyphConverter::glyphsToUnichars(const Font& font,
                                                      const std::vector<GlyphID>& glyphs) {
  auto typeface = font.getTypeface();
  if (!typeface) {
    return {};
  }
  std::vector<Unichar> result(glyphs.size(), 0);
  auto glyphMap = getGlyphToUnicodeMap(typeface);
  for (size_t i = 0; i < glyphs.size(); i++) {
    result[i] = glyphMap[glyphs[i]];
  }
  return result;
}

const std::vector<Unichar>& GlyphConverter::getGlyphToUnicodeMap(
    const std::shared_ptr<Typeface>& typeface) {
  auto iter = fontToGlyphMap.find(typeface->uniqueID());
  if (iter != fontToGlyphMap.end()) {
    return iter->second;
  }
  fontToGlyphMap[typeface->uniqueID()] = typeface->getGlyphToUnicodeMap();
  return fontToGlyphMap[typeface->uniqueID()];
}

#endif

}  // namespace tgfx