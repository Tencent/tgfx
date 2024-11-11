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

#include "tgfx/layers/TextLayer.h"
#include "core/utils/Log.h"
#include "layers/contents/TextContent.h"
#include "tgfx/core/UTF.h"

namespace tgfx {

static std::mutex& TypefaceMutex = *new std::mutex;
static std::vector<std::shared_ptr<Typeface>> FallbackTypefaces = {};

void TextLayer::SetFallbackTypefaces(std::vector<std::shared_ptr<Typeface>> typefaces) {
  std::lock_guard<std::mutex> lock(TypefaceMutex);
  FallbackTypefaces = std::move(typefaces);
}

std::vector<std::shared_ptr<Typeface>> GetFallbackTypefaces() {
  std::lock_guard<std::mutex> lock(TypefaceMutex);
  return FallbackTypefaces;
}

std::shared_ptr<TextLayer> TextLayer::Make() {
  auto layer = std::shared_ptr<TextLayer>(new TextLayer());
  layer->weakThis = layer;
  return layer;
}

void TextLayer::setText(const std::string& text) {
  if (_text == text) {
    return;
  }
  _text = text;
  invalidateContent();
}

void TextLayer::setTextColor(const Color& color) {
  if (_textColor == color) {
    return;
  }
  _textColor = color;
  invalidateContent();
}

void TextLayer::setFont(const Font& font) {
  if (_font == font) {
    return;
  }
  _font = font;
  invalidateContent();
}

void TextLayer::setWidth(float width) {
  if (width < 0) {
    width = 0;
  }
  if (_width == width) {
    return;
  }
  _width = width;
  invalidateContent();
}

void TextLayer::setHeight(float height) {
  if (height < 0) {
    height = 0;
  }
  if (_height == height) {
    return;
  }
  _height = height;
  invalidateContent();
}

void TextLayer::setTextAlign(TextAlign align) {
  if (_textAlign == align) {
    return;
  }
  _textAlign = align;
  invalidateContent();
}

void TextLayer::setAutoWrap(bool value) {
  if (_autoWrap == value) {
    return;
  }
  _autoWrap = value;
  invalidateContent();
}

std::unique_ptr<LayerContent> TextLayer::onUpdateContent() {
  if (_text.empty()) {
    return nullptr;
  }

  // preprocess newlines, convert \r\n, \r to \n
  const std::string text = preprocessNewLines(_text);

  const auto textShaperGlyphs = shapeText(text, _font.getTypeface());
  if (nullptr == textShaperGlyphs) {
    LOGE("TextLayer::onUpdateContent textShaperGlyphs is nullptr.");
    return nullptr;
  }

  float xOffset = 0.0f;
  float yOffset = getLineHeight(textShaperGlyphs, 0, 0);
  size_t newLineStartIndex = 0;
  size_t glyphCountPerLine = 0;
  std::vector<Point> positions = {};
  std::vector<GlyphID> glyphIDs = {};
  std::vector<std::pair<size_t, size_t>> lineBreakIndices = {};
  const auto glyphSize = textShaperGlyphs->size();
  for (size_t i = 0; i < glyphSize; i++) {
    const auto characterUnicode = textShaperGlyphs->getCharacterUnicode(i);
    const auto glyphID = textShaperGlyphs->getGlyphID(i);
    if ('\u000A' == characterUnicode) {
      xOffset = 0.0f;
      yOffset += getLineHeight(textShaperGlyphs, newLineStartIndex, glyphCountPerLine);
      lineBreakIndices.emplace_back(newLineStartIndex, glyphCountPerLine);
      newLineStartIndex = positions.size();
      glyphCountPerLine = 0;
    } else {
      float advance = 0.0f;
      const auto typeface = textShaperGlyphs->getTypeface(i);
      if (nullptr == typeface || _font.getTypeface() == typeface) {
        advance = _font.getAdvance(glyphID);
      } else {
        auto font = _font;
        font.setTypeface(typeface);
        advance = font.getAdvance(glyphID);
      }
      if (_autoWrap && xOffset + advance > _width) {
        xOffset = 0;
        yOffset += getLineHeight(textShaperGlyphs, newLineStartIndex, glyphCountPerLine);
        lineBreakIndices.emplace_back(newLineStartIndex, glyphCountPerLine);
        newLineStartIndex = positions.size();
        glyphCountPerLine = 0;
      }
      positions.emplace_back(Point::Make(xOffset, yOffset));
      glyphIDs.emplace_back(glyphID);
      glyphCountPerLine++;
      xOffset += advance;
    }
  }
  lineBreakIndices.emplace_back(newLineStartIndex, glyphCountPerLine);

#if 1
  printf("textShaperGlyphs->size() = %lu, positions.size() = %lu, glyphIDs.size() = %lu\n", textShaperGlyphs->size(), positions.size(), glyphIDs.size());
  printf("lineBreakIndices.size() = %lu\n", lineBreakIndices.size());
  for (const auto& lineBreakIndex : lineBreakIndices) {
    printf("lineBreakIndex.first = %lu, lineBreakIndex.second = %lu\n", lineBreakIndex.first,
           lineBreakIndex.second);
  }
#endif

  // Handle text alignment
  resolveTextAlignment(positions, lineBreakIndices);

  GlyphRun glyphRun(_font, std::move(glyphIDs), std::move(positions));
  auto textBlob = TextBlob::MakeFrom(std::move(glyphRun));
  if (nullptr == textBlob) {
    return nullptr;
  }
  return std::make_unique<TextContent>(std::move(textBlob), _textColor);
}

std::string TextLayer::preprocessNewLines(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  const size_t size = text.size();

  for (size_t i = 0; i < size; ++i) {
    if ('\r' == text[i]) {
      if (i + 1 < size && '\n' == text[i + 1]) {
        result += '\n';
        ++i;
      } else {
        result += '\n';
      }
    } else {
      result += text[i];
    }
  }

  return result;
}

float TextLayer::getLineHeight(const std::shared_ptr<TextShaperGlyphs>& textShaperGlyphs,
                               size_t startIndex, size_t count) const {
  const auto fontMetrics = _font.getMetrics();
  float ascent = std::fabs(fontMetrics.ascent);
  float descent = std::fabs(fontMetrics.descent);
  float leading = std::fabs(fontMetrics.leading);
  std::shared_ptr<Typeface> lastTypeface = nullptr;

  for (size_t i = startIndex; i < startIndex + count && i < textShaperGlyphs->size(); ++i) {
    const auto typeface = textShaperGlyphs->getTypeface(i);
    if (nullptr == typeface || lastTypeface == typeface) {
      continue;
    }

    lastTypeface = typeface;
    auto font = _font;
    font.setTypeface(typeface);
    const auto fontMetrics = font.getMetrics();
    ascent = std::max(ascent, std::fabs(fontMetrics.ascent));
    descent = std::max(descent, std::fabs(fontMetrics.descent));
    leading = std::max(leading, std::fabs(fontMetrics.leading));
  }

  return ascent + descent + leading;
}

std::shared_ptr<TextLayer::TextShaperGlyphs> TextLayer::shapeText(
    const std::string& text, const std::shared_ptr<Typeface>& typeface) {
  if (text.empty()) {
    return nullptr;
  }

  const char* head = text.data();
  const char* tail = head + text.size();
  auto fallbackTypefaces = GetFallbackTypefaces();
  auto glyphs = std::make_shared<TextShaperGlyphs>();

  while (head < tail) {
    if ('\n' == *head) {
      const GlyphID lineFeedCharacterGlyphID = typeface ? typeface->getGlyphID('\u000A') : 0;
      glyphs->append('\u000A', lineFeedCharacterGlyphID, typeface);
      ++head;
      continue;
    }

    const auto characterUnicode = UTF::NextUTF8(&head, tail);
    GlyphID glyphID = typeface ? typeface->getGlyphID(characterUnicode) : 0;
    if (glyphID <= 0) {
      for (const auto& fallbackTypeface : fallbackTypefaces) {
        if (nullptr != fallbackTypeface) {
          glyphID = fallbackTypeface->getGlyphID(characterUnicode);
          if (glyphID > 0) {
            glyphs->append(characterUnicode, glyphID, fallbackTypeface);
            break;
          }
        }
      }
    } else {
      glyphs->append(characterUnicode, glyphID, typeface);
    }
  }

  return glyphs;
}

void TextLayer::resolveTextAlignment(const std::vector<Point>& /*positions*/,
                                     const std::vector<std::pair<size_t, size_t>>& /*lineBreakIndices*/) {
  // TODO: Implement text alignment
}
}  // namespace tgfx
