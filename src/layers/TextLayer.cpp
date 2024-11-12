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

  // 1. preprocess newlines, convert \r\n, \r to \n
  const std::string text = preprocessNewLines(_text);

  // 2. shape text to glyphs, handle font fallback
  const auto textShaperGlyphs = shapeText(text, _font.getTypeface());
  if (nullptr == textShaperGlyphs) {
    LOGE("TextLayer::onUpdateContent textShaperGlyphs is nullptr.");
    return nullptr;
  }

  // 3. Handle text wrapping and auto-wrapping
  std::vector<std::shared_ptr<OneLineGlyphs>> glyphLines = {};
  auto glyphLine = std::make_shared<OneLineGlyphs>();
  auto fontMetrics = _font.getMetrics();
  float xOffset = 0.0f;
  float yOffset = std::fabs(fontMetrics.ascent) + std::fabs(fontMetrics.descent) +
                  std::fabs(fontMetrics.leading);
  const auto glyphInfos = textShaperGlyphs->getGlyphInfos();
  for (size_t i = 0; i < glyphInfos.size(); i++) {
    const auto& glyphInfo = glyphInfos[i];
    const auto characterUnicode = glyphInfo->getCharacterUnicode();
    if ('\u000A' == characterUnicode) {
      glyphLine->setLineWidth(xOffset);
      glyphLine->setLineHeight(yOffset);
      glyphLines.emplace_back(glyphLine);

      xOffset = 0.0f;
      yOffset += getLineHeight(glyphLine);

      glyphLine = std::make_shared<OneLineGlyphs>();
    } else {
      const float advance = glyphInfo->getAdvance();
      if (_autoWrap && xOffset + advance > _width) {
        glyphLine->setLineWidth(xOffset);
        glyphLine->setLineHeight(yOffset);
        glyphLines.emplace_back(glyphLine);

        xOffset = 0;
        yOffset += getLineHeight(glyphLine);

        glyphLine = std::make_shared<OneLineGlyphs>();
      }
      glyphLine->append(glyphInfo, Point::Make(xOffset, yOffset));
      xOffset += advance;
    }
  }
  glyphLine->setLineWidth(xOffset);
  glyphLine->setLineHeight(yOffset);
  glyphLines.emplace_back(glyphLine);

  // 4. Handle text alignment
  resolveTextAlignment(glyphLines);

  // 5. Calculate the final glyphs and positions for rendering
  std::vector<GlyphID> glyphIDs = {};
  std::vector<Point> positions = {};
  for (const auto& line : glyphLines) {
    const auto lineGlyphCount = line->getGlyphCount();
    for (size_t i = 0; i < lineGlyphCount; ++i) {
      glyphIDs.push_back(line->getGlyphInfo(i)->getGlyphID());
      positions.push_back(line->getPosition(i));
    }
  }

#if 1
  printf("textShaperGlyphs->getGlyphInfos().size() = %lu\n",
         textShaperGlyphs->getGlyphInfos().size());
  printf("glyphIDs.size() = %lu, positions.size() = %lu\n", glyphIDs.size(), positions.size());
  printf("glyphLines.size() = %lu\n", glyphLines.size());
  for (const auto& line : glyphLines) {
    printf("lineWidth = %f, lineHeight = %f, line->getGlyphCount() = %lu\n", line->lineWidth(),
           line->lineHeight(), line->getGlyphCount());
  }
#endif

  GlyphRun glyphRun(_font, std::move(glyphIDs), std::move(positions));
  auto textBlob = TextBlob::MakeFrom(std::move(glyphRun));
  if (nullptr == textBlob) {
    return nullptr;
  }
  return std::make_unique<TextContent>(std::move(textBlob), _textColor);
}

std::string TextLayer::preprocessNewLines(const std::string& text) const {
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

float TextLayer::getLineHeight(const std::shared_ptr<OneLineGlyphs>& oneLineGlyphs) const {
  if (nullptr == oneLineGlyphs) {
    return 0.0f;
  }

  float ascent = 0.0f;
  float descent = 0.0f;
  float leading = 0.0f;
  std::shared_ptr<Typeface> lastTypeface = nullptr;

  const auto size = oneLineGlyphs->getGlyphCount();
  for (size_t i = 0; i < size; ++i) {
    const auto& typeface = oneLineGlyphs->getGlyphInfo(i)->getTypeface();
    if (nullptr == typeface || lastTypeface == typeface) {
      continue;
    }

    lastTypeface = typeface;
    auto font = _font;
    font.setTypeface(typeface);
    const auto& fontMetrics = font.getMetrics();
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
      glyphs->append('\u000A', lineFeedCharacterGlyphID, 0.0f, typeface);
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
            auto font = _font;
            font.setTypeface(fallbackTypeface);
            glyphs->append(characterUnicode, glyphID, font.getAdvance(glyphID), fallbackTypeface);
            break;
          }
        }
      }
    } else {
      float advance = 0.0f;
      if (typeface == _font.getTypeface()) {
        advance = _font.getAdvance(glyphID);
      } else {
        auto font = _font;
        font.setTypeface(typeface);
        advance = font.getAdvance(glyphID);
      }
      glyphs->append(characterUnicode, glyphID, advance, typeface);
    }
  }

  return glyphs;
}

void TextLayer::resolveTextAlignment(
    const std::vector<std::shared_ptr<OneLineGlyphs>>& glyphLines) const {
  if (glyphLines.empty()) {
    return;
  }

  for (const auto& glyphLine : glyphLines) {
    const auto lineWidth = glyphLine->lineWidth();
    const auto lineGlyphCount = glyphLine->getGlyphCount();

    float xOffset = 0.0f;
    float spaceWidth = 0.0f;
    switch (_textAlign) {
      case TextAlign::Left:
        // do nothing
        break;
      case TextAlign::Center:
        xOffset = (_width - lineWidth) / 2.0f;
        break;
      case TextAlign::Right:
        xOffset = _width - lineWidth;
        break;
      case TextAlign::Justify: {
        if (lineGlyphCount > 1) {
          spaceWidth = (_width - lineWidth) / (lineGlyphCount - 1);
        }
        break;
      }
      default:
        break;
    }

    for (size_t i = 0; i < lineGlyphCount; ++i) {
      auto& position = glyphLine->getPosition(i);
      if (lineGlyphCount >= 1) {
        position.x += xOffset + spaceWidth * i;
      } else {
        position.x += xOffset;
      }
    }
  }
}
}  // namespace tgfx
