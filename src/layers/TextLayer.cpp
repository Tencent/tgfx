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

std::unique_ptr<LayerContent> TextLayer::onUpdateContent() {
  if (_text.empty()) {
    return nullptr;
  }

  const std::string text = preprocessNewLines(_text);

  std::vector<GlyphID> glyphs = {};
  std::vector<Point> positions = {};
  std::vector<OneLineParam> oneLineParams = {};

  // space character glyph id
  const auto [spaceCharacterGlyphID, spaceCharacterTypeface] = getGlyphIDAndTypeface('\u0020');
  const auto emptyAdvance = getEmptyAdvance();

  float xOffset = 0;
  float yOffset = getLineHeight(oneLineParams);
  const char* head = text.data();
  const char* tail = head + text.size();
  // refer to:
  // https://developer.apple.com/library/archive/documentation/StringsTextFonts/Conceptual/TextAndWebiPhoneOS/TypoFeatures/TextSystemFeatures.html
  // https://codesign-1252678369.cos.ap-guangzhou.myqcloud.com/text_layout.png
  while (head < tail) {
    if ('\n' == *head) {
      xOffset = 0;
      yOffset += getLineHeight(oneLineParams);
      ++head;

      resolveTextAlignment(positions, oneLineParams);
      oneLineParams.clear();
    } else if ('\t' == *head) {
      // tab width default is 4 spaces
      for (uint32_t i = 0; i < 4; i++) {
        if (_autoWrap && xOffset + emptyAdvance > _width) {
          xOffset = 0;
          yOffset += getLineHeight(oneLineParams);

          resolveTextAlignment(positions, oneLineParams);
          oneLineParams.clear();
        }

        if (0 == spaceCharacterGlyphID) {
          LOGE("Space character glyph id is 0.");
          continue;
        }

        glyphs.push_back(spaceCharacterGlyphID);
        const auto point = Point::Make(xOffset, yOffset);
        positions.push_back(point);
        xOffset += emptyAdvance;
        oneLineParams.push_back({positions.size() - 1, point, spaceCharacterTypeface});
      }
      ++head;
    } else {
      const auto character = UTF::NextUTF8(&head, tail);
      const auto glyphIDAndTypeface = getGlyphIDAndTypeface(character);
      const auto glyphID = glyphIDAndTypeface.first;
      float advance = emptyAdvance;
      if (glyphID > 0) {
        auto font = _font;
        font.setTypeface(glyphIDAndTypeface.second);
        advance = font.getAdvance(glyphID);
      }

      if (_autoWrap && xOffset + advance > _width) {
        xOffset = 0;
        yOffset += getLineHeight(oneLineParams);

        resolveTextAlignment(positions, oneLineParams);
        oneLineParams.clear();
      }

      const auto tmpGlyphID = glyphID > 0 ? glyphID : spaceCharacterGlyphID;
      if (0 == tmpGlyphID) {
        LOGE("tmpGlyphID id is 0.");
        continue;
      }

      glyphs.push_back(tmpGlyphID);
      const auto point = Point::Make(xOffset, yOffset);
      positions.push_back(point);
      xOffset += advance;
      oneLineParams.push_back({positions.size() - 1, point, glyphIDAndTypeface.second});
    }
  }

  GlyphRun glyphRun(_font, std::move(glyphs), std::move(positions));
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

std::pair<GlyphID, std::shared_ptr<Typeface>> TextLayer::getGlyphIDAndTypeface(Unichar unichar) const {
  auto glyphID = _font.getGlyphID(unichar);
  if (glyphID > 0) {
    return {glyphID, _font.getTypeface()};
  }

  const auto& fallbackTypefaces = GetFallbackTypefaces();
  for (const auto& fallbackTypeface : fallbackTypefaces) {
    if (nullptr == fallbackTypeface) {
      continue;
    }

    glyphID = fallbackTypeface->getGlyphID(unichar);
    if (glyphID > 0) {
      return {glyphID, fallbackTypeface};
    }
  }

  return {0, nullptr};
}

float TextLayer::getEmptyAdvance() const {
  if (_font.getMetrics().xHeight > 0.0f) {
    return _font.getMetrics().xHeight;
  }

  const auto& fallbackTypefaces = GetFallbackTypefaces();
  for (const auto& fallbackTypeface : fallbackTypefaces) {
    if (nullptr == fallbackTypeface) {
      continue;
    }
    auto font = _font;
    font.setTypeface(fallbackTypeface);
    if (font.getMetrics().xHeight > 0.0f) {
      return font.getMetrics().xHeight;
    }
  }

  return 0.0f;
}

float TextLayer::getLineHeight(const std::vector<OneLineParam>& oneLineParams) const {
  const auto fontMetrics = _font.getMetrics();
  float ascent = std::fabs(fontMetrics.ascent);
  float descent = std::fabs(fontMetrics.descent);
  float leading = std::fabs(fontMetrics.leading);
  std::shared_ptr<Typeface> lastTypeface = nullptr;

  for (const auto& oneLineParam : oneLineParams) {
    if (nullptr == oneLineParam.typeface || lastTypeface == oneLineParam.typeface) {
      continue;
    }

    lastTypeface = oneLineParam.typeface;
    auto font = _font;
    font.setTypeface(oneLineParam.typeface);
    const auto fontMetrics = font.getMetrics();
    ascent = std::max(ascent, std::fabs(fontMetrics.ascent));
    descent = std::max(descent, std::fabs(fontMetrics.descent));
    leading = std::max(leading, std::fabs(fontMetrics.leading));
  }

  return ascent + descent + leading;
}

void TextLayer::resolveTextAlignment(const std::vector<Point>& /*positions*/,
                                     const std::vector<OneLineParam>& /*oneLineParams*/) {
  // TODO: Implement text alignment
}
}  // namespace tgfx
