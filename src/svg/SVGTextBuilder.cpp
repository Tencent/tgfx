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

#include "SVGTextBuilder.h"
#include <cstdint>
#include <string>
#include "core/utils/FontUtils.h"
#include "core/utils/MathExtra.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"

namespace tgfx {

SVGTextBuilder::SVGTextBuilder(const GlyphRun& glyphRun) {
  if (auto typeface = glyphRun.font.getTypeface()) {
    auto unichars = FontUtils::GlyphsToUnichars(glyphRun.font, glyphRun.glyphs);
    for (uint32_t i = 0; i < unichars.size(); i++) {
      this->appendUnichar(unichars[i], glyphRun.positions[i]);
    }
  }
}

void SVGTextBuilder::appendUnichar(Unichar c, Point position) {
  bool discardPos = false;
  bool isWhitespace = false;

  switch (c) {
    case ' ':
    case '\t':
      // consolidate whitespace to match SVG's xml:space=default munging
      // (http://www.w3.org/TR/SVG/text.html#WhiteSpace)
      if (lastCharWasWhitespace) {
        discardPos = true;
      } else {
        _text.append(UTF::ToUTF8(c));
      }
      isWhitespace = true;
      break;
    case '\0':
      // '\0' for inconvertible glyphs, but these are not legal XML characters
      // (http://www.w3.org/TR/REC-xml/#charsets)
      discardPos = true;
      isWhitespace = lastCharWasWhitespace;  // preserve whitespace consolidation
      break;
    case '&':
      _text.append("&amp;");
      break;
    case '"':
      _text.append("&quot;");
      break;
    case '\'':
      _text.append("&apos;");
      break;
    case '<':
      _text.append("&lt;");
      break;
    case '>':
      _text.append("&gt;");
      break;
    default:
      _text.append(UTF::ToUTF8(c));
      break;
  }

  lastCharWasWhitespace = isWhitespace;

  if (discardPos) {
    return;
  }

  posXString.append(FloatToString(position.x) + ", ");
  posYString.append(FloatToString(position.y) + ", ");

  if (constYString.empty()) {
    constYString = posYString;
    constY = position.y;
  } else {
    hasConstY &= FloatNearlyEqual(constY, position.y);
  }
}

}  // namespace tgfx