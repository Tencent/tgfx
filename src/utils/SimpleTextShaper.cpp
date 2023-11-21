/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "SimpleTextShaper.h"
#include "tgfx/utils/UTF.h"

namespace tgfx {
std::pair<std::vector<GlyphID>, std::vector<Point>> SimpleTextShaper::Shape(
    const std::string& text, const tgfx::Font& font) {
  const char* textStart = text.data();
  const char* textStop = textStart + text.size();
  std::vector<GlyphID> glyphs = {};
  std::vector<Point> positions = {};
  auto emptyGlyphID = font.getGlyphID(" ");
  auto emptyAdvance = font.getAdvance(emptyGlyphID);
  float xOffset = 0;
  while (textStart < textStop) {
    auto unichar = UTF::NextUTF8(&textStart, textStop);
    auto glyphID = font.getGlyphID(unichar);
    if (glyphID > 0) {
      glyphs.push_back(glyphID);
      positions.push_back(Point::Make(xOffset, 0.0f));
      auto advance = font.getAdvance(glyphID);
      xOffset += advance;
    } else {
      xOffset += emptyAdvance;
    }
  }
  return {glyphs, positions};
}
}  // namespace tgfx
