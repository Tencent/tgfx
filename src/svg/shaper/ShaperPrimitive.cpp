/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "svg/shaper/ShaperPrimitive.h"
#include "core/utils/Log.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"

namespace tgfx {

namespace {

inline bool is_breaking_whitespace(Unichar c) {
  switch (c) {
    case 0x0020:  // SPACE
    //case 0x00A0: // NO-BREAK SPACE
    case 0x1680:  // OGHAM SPACE MARK
    case 0x180E:  // MONGOLIAN VOWEL SEPARATOR
    case 0x2000:  // EN QUAD
    case 0x2001:  // EM QUAD
    case 0x2002:  // EN SPACE (nut)
    case 0x2003:  // EM SPACE (mutton)
    case 0x2004:  // THREE-PER-EM SPACE (thick space)
    case 0x2005:  // FOUR-PER-EM SPACE (mid space)
    case 0x2006:  // SIX-PER-EM SPACE
    case 0x2007:  // FIGURE SPACE
    case 0x2008:  // PUNCTUATION SPACE
    case 0x2009:  // THIN SPACE
    case 0x200A:  // HAIR SPACE
    case 0x200B:  // ZERO WIDTH SPACE
    case 0x202F:  // NARROW NO-BREAK SPACE
    case 0x205F:  // MEDIUM MATHEMATICAL SPACE
    case 0x3000:  // IDEOGRAPHIC SPACE
                  //case 0xFEFF: // ZERO WIDTH NO-BREAK SPACE
      return true;
    default:
      return false;
  }
}

size_t linebreak(const char text[], const char stop[], float width, const float* advance,
                 size_t* trailing) {
  float accumulatedWidth = 0;
  int glyphIndex = 0;
  const char* start = text;
  const char* wordStart = text;
  bool prevWS = true;
  *trailing = 0;

  while (text < stop) {
    const char* prevText = text;
    Unichar uni = UTF::NextUTF8(&text, stop);
    accumulatedWidth += advance[glyphIndex++];
    bool currWS = is_breaking_whitespace(uni);

    if (!currWS && prevWS) {
      wordStart = prevText;
    }
    prevWS = currWS;

    if (width < accumulatedWidth) {
      bool consumeWhitespace = false;
      if (currWS) {
        // previous fit, put this and following whitespace in trailing
        if (prevText == start) {
          // don't put this in trailing if it's the first thing
          prevText = text;
        }
        consumeWhitespace = true;
      } else if (wordStart != start) {
        // backup to the last whitespace that fit
        text = wordStart;
      } else if (prevText > start) {
        // backup to just before the glyph that didn't fit
        text = prevText;
      } else {
        // let it overflow, put any following whitespace in trailing
        prevText = text;
        consumeWhitespace = true;
      }
      if (consumeWhitespace) {
        const char* next = text;
        while (next < stop && is_breaking_whitespace(UTF::NextUTF8(&next, stop))) {
          text = next;
        }
        if (trailing) {
          *trailing = static_cast<size_t>(text - prevText);
        }
      }
      break;
    }
  }
  return static_cast<size_t>(text - start);
}

}  // namespace

void ShaperPrimitive::shape(const char* utf8, size_t utf8Bytes, FontRunIterator& fontIter,
                            BiDiRunIterator& /*bidiIter*/, ScriptRunIterator& /*scriptIter*/,
                            LanguageRunIterator& /*langIter*/, const RunFeature* /*features*/,
                            size_t /*featuresSize*/, float width, RunHandler* handler) const {
  Font font;
  if (!fontIter.atEnd()) {
    fontIter.consume();
    font = fontIter.currentFont();
  }
  DEBUG_ASSERT(font.getTypeface());

  std::vector<GlyphID> glyphs;
  std::vector<float> advances;

  {
    const char* textStart = utf8;
    const char* textStop = utf8 + utf8Bytes;
    while (textStart < textStop) {
      auto unichar = UTF::NextUTF8(&textStart, textStop);
      auto glyphID = font.getGlyphID(unichar);
      glyphs.push_back(glyphID);
      advances.push_back(font.getAdvance(glyphID));
    }
  }

  int glyphOffset = 0;
  size_t utf8Offset = 0;
  do {
    size_t bytesCollapsed = 0;
    size_t bytesConsumed =
        linebreak(utf8, utf8 + utf8Bytes, width, advances.data() + glyphOffset, &bytesCollapsed);
    size_t bytesVisible = bytesConsumed - bytesCollapsed;

    auto numGlyphs = static_cast<uint32_t>(UTF::CountUTF8(utf8, bytesVisible));

    const char* textStart = utf8;
    const char* textStop = utf8 + utf8Bytes;
    float runWidth = 0.0f;
    while (textStart < textStop) {
      auto unichar = UTF::NextUTF8(&textStart, textStop);
      auto glyphID = font.getGlyphID(unichar);
      runWidth += font.getAdvance(glyphID) + font.getBounds(glyphID).width();
    }

    const RunHandler::RunInfo info = {font,
                                      0,
                                      {runWidth, 0},
                                      numGlyphs,
                                      RunHandler::Range(utf8Offset, bytesVisible)};

    handler->beginLine();
    if (info.glyphCount) {
      handler->runInfo(info);
    }
    handler->commitRunInfo();
    if (info.glyphCount) {
      const auto buffer = handler->runBuffer(info);

      memcpy(buffer.glyphs, glyphs.data() + glyphOffset, info.glyphCount * sizeof(GlyphID));
      auto position = buffer.point;
      for (size_t i = 0; i < info.glyphCount; ++i) {
        buffer.positions[i] = position;
        position.x += advances[i + static_cast<size_t>(glyphOffset)];
      }
      if (buffer.clusters) {
        const auto* textPointer = utf8;
        for (size_t i = 0; i < info.glyphCount; ++i) {
          // Each character maps to exactly one glyph.
          buffer.clusters[i] =
              static_cast<uint32_t>(static_cast<size_t>(textPointer - utf8) + utf8Offset);
          UTF::NextUTF8(&textPointer, utf8 + utf8Bytes);
        }
      }
      handler->commitRunBuffer(info);
    }
    handler->commitLine();

    glyphOffset += UTF::CountUTF8(utf8, bytesConsumed);
    utf8Offset += bytesConsumed;
    utf8 += bytesConsumed;
    utf8Bytes -= bytesConsumed;
  } while (0 < utf8Bytes);
}

}  // namespace tgfx