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
#include <regex>

namespace tgfx {

//static constexpr float DefaultLineHeight = 1.2f;
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

void TextLayer::setVerticalAlign(VerticalAlign value) {
  if (_verticalAlign == value) {
    return;
  }
  _verticalAlign = value;
  invalidateContent();
}

void TextLayer::setAutoWrap(bool value) {
  if (_autoWrap == value) {
    return;
  }
  _autoWrap = value;
  invalidateContent();
}

void TextLayer::setTabSpaceNum(uint32_t num) {
  if (_tabSpaceNum == num) {
    return;
  }

  _tabSpaceNum = num;
  invalidateContent();
}

#if 0
std::unique_ptr<LayerContent> TextLayer::onUpdateContent() {
  if (_text.empty()) {
    return nullptr;
  }
  std::vector<GlyphID> glyphs = {};
  std::vector<Point> positions = {};

  // use middle alignment, refer to the document: https://paddywang.github.io/demo/list/css/baseline_line-height.html
  auto metrics = _font.getMetrics();
  auto lineHeight = ceil(_font.getSize() * DefaultLineHeight);
  auto baseLine = (lineHeight + metrics.xHeight) / 2;
  auto emptyGlyphID = _font.getGlyphID(" ");
  auto emptyAdvance = _font.getAdvance(emptyGlyphID);
  const char* textStart = _text.data();
  const char* textStop = textStart + _text.size();
  float xOffset = 0;
  float yOffset = baseLine;
  while (textStart < textStop) {
    if (*textStart == '\n') {
      xOffset = 0;
      yOffset += lineHeight;
      glyphs.push_back(emptyGlyphID);
      positions.push_back(Point::Make(xOffset, yOffset));
      textStart++;
      continue;
    }
    auto unichar = UTF::NextUTF8(&textStart, textStop);
    auto glyphID = _font.getGlyphID(unichar);
    if (glyphID > 0) {
      glyphs.push_back(glyphID);
      positions.push_back(Point::Make(xOffset, yOffset));
      auto advance = _font.getAdvance(glyphID);
      xOffset += advance;
    } else {
      glyphs.push_back(emptyGlyphID);
      positions.push_back(Point::Make(xOffset, yOffset));
      xOffset += emptyAdvance;
    }
  }
  GlyphRun glyphRun(_font, std::move(glyphs), std::move(positions));
  auto textBlob = TextBlob::MakeFrom(std::move(glyphRun));
  if (textBlob == nullptr) {
    return nullptr;
  }
  return std::make_unique<TextContent>(std::move(textBlob), _textColor);
}
#endif

#if 1
std::unique_ptr<LayerContent> TextLayer::onUpdateContent() {
  if (_text.empty()) {
    return nullptr;
  }

  std::string text = preprocessNewLines(_text);

  std::vector<GlyphID> glyphs = {};
  std::vector<Point> positions = {};
  const auto spaceGlyphID = _font.getGlyphID(" ");
  const auto spaceAdvance = _font.getAdvance(spaceGlyphID);
  const auto fontMetrics = _font.getMetrics();
  const float lineHeight = std::fabs(fontMetrics.ascent) + std::fabs(fontMetrics.descent) +
                           std::fabs(fontMetrics.leading);
  float xOffset = 0;
  float yOffset = std::fabs(fontMetrics.ascent);
  const char* head = text.data();
  const char* tail = head + text.size();
  // refer to:
  // https://developer.apple.com/library/archive/documentation/StringsTextFonts/Conceptual/TextAndWebiPhoneOS/TypoFeatures/TextSystemFeatures.html
  // https://codesign-1252678369.cos.ap-guangzhou.myqcloud.com/text_layout.png
  while (head < tail) {
    const auto character = UTF::NextUTF8(&head, tail);
    const auto glyphID = _font.getGlyphID(character);
    if (glyphID > 0) {
      glyphs.push_back(glyphID);
      positions.push_back(Point::Make(xOffset, yOffset));
      auto advance = _font.getAdvance(glyphID);
      xOffset += advance;

      if (_autoWrap) {
        if (xOffset >= _width) {
          xOffset = 0;
          yOffset += lineHeight;
        }
      }
    } else {
#if DEBUG
      const auto pos = text.find(static_cast<char>(character));
      LOGE("invalid character: %c(%d), pos : %d, glyphID is 0.", character, character, pos);
#endif
      if ('\n' == character) {
        xOffset = 0;
        yOffset += lineHeight;
      } else if ('\t' == character) {
        for (uint32_t i=0; i < _tabSpaceNum; i++) {
          glyphs.push_back(spaceGlyphID);
          positions.push_back(Point::Make(xOffset, yOffset));
          xOffset += spaceAdvance;

          if (_autoWrap) {
            if (xOffset >= _width) {
              xOffset = 0;
              yOffset += lineHeight;
            }
          }
        }
      }
    }
  }

  GlyphRun glyphRun(_font, std::move(glyphs), std::move(positions));
  auto textBlob = TextBlob::MakeFrom(std::move(glyphRun));
  if (nullptr == textBlob) {
    return nullptr;
  }
  return std::make_unique<TextContent>(std::move(textBlob), _textColor);
}
#endif

std::string TextLayer::preprocessNewLines(const std::string& text) {
  std::regex newlineRegex(R"(\r\n|\n\r|\r)");
  return std::regex_replace(text, newlineRegex, "\n");
}
}  // namespace tgfx
