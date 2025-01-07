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
#include "core/FontGlyphFace.h"
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
  const std::string text = PreprocessNewLines(_text);

  // 2. shape text to glyphs, handle font fallback
  const auto& glyphInfos = ShapeText(text, _font.getTypeface());
  if (glyphInfos.empty()) {
    return nullptr;
  }

  // 3. Handle text wrapping and auto-wrapping
  std::vector<std::shared_ptr<GlyphLine>> glyphLines = {};
  auto glyphLine = std::make_shared<GlyphLine>();
  const auto emptyAdvance = _font.getSize() / 2.0f;
  float xOffset = 0;
  for (size_t i = 0; i < glyphInfos.size(); i++) {
    const auto& glyphInfo = glyphInfos[i];
    const auto characterUnicode = glyphInfo->getUnichar();
    if ('\n' == characterUnicode) {
      xOffset = 0;
      glyphLines.emplace_back(glyphLine);
      glyphLine = std::make_shared<GlyphLine>();
    } else {
      const float advance = calcAdvance(glyphInfo, emptyAdvance);
      // If _width is 0, auto-wrap is disabled and no wrapping will occur.
      if (_autoWrap && (0.0f != _width) && (xOffset + advance > _width)) {
        xOffset = 0;
        if (glyphLine->getGlyphCount() > 0) {
          glyphLines.emplace_back(glyphLine);
          glyphLine = std::make_shared<GlyphLine>();
        }
      }
      glyphLine->append(glyphInfo, advance);
      xOffset += advance;
    }
  }
  if (glyphLine->getGlyphCount() > 0) {
    glyphLines.emplace_back(glyphLine);
  }

  // 4. Adjust the number of text lines based on _height
  TruncateGlyphLines(glyphLines);

  // 5. Handle text alignment
  std::vector<std::shared_ptr<GlyphInfo>> finalGlyphs = {};
  std::vector<Point> positions = {};
  resolveTextAlignment(glyphLines, emptyAdvance, finalGlyphs, positions);
  if (finalGlyphs.size() != positions.size()) {
    LOGE("TextLayer::onUpdateContent finalGlyphs.size() != positions.size(), error.");
    return nullptr;
  }

  // 6. Calculate the final glyphs and positions for rendering
  std::vector<GlyphRun> glyphRunList;
  buildGlyphRunList(finalGlyphs, positions, glyphRunList);

  auto textBlob = TextBlob::MakeFrom(std::move(glyphRunList));
  if (nullptr == textBlob) {
    return nullptr;
  }

  return std::make_unique<TextContent>(std::move(textBlob), _textColor);
}

std::string TextLayer::PreprocessNewLines(const std::string& text) {
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

float TextLayer::calcAdvance(const std::shared_ptr<GlyphInfo>& glyphInfo,
                             float emptyAdvance) const {
  if (glyphInfo == nullptr) {
    return 0.0f;
  }

  float advance = 0.0f;
  const auto glyphID = glyphInfo->getGlyphID();
  const auto typeface = glyphInfo->getTypeface();
  if (glyphID <= 0 || typeface == nullptr) {
    advance = emptyAdvance;
  } else {
    auto font = _font;
    font.setTypeface(typeface);
    advance = font.getAdvance(glyphID);
  }

  return advance;
}

float TextLayer::getLineHeight(const std::shared_ptr<GlyphLine>& glyphLine) const {
  if (glyphLine == nullptr) {
    return 0.0f;
  }

  // For a blank line with only newline characters, use the font's ascent, descent, and leading as the line height.
  if (glyphLine->getGlyphCount() == 0) {
    const auto fontMetrics = _font.getMetrics();
    return std::fabs(fontMetrics.ascent) + std::fabs(fontMetrics.descent) +
           std::fabs(fontMetrics.leading);
  }

  float lineHeight = 0.0f;
  std::unordered_map<uint32_t, float> fontHeightMap = {};

  const auto size = glyphLine->getGlyphCount();
  for (size_t i = 0; i < size; ++i) {
    const auto& typeface = glyphLine->getGlyphInfo(i)->getTypeface();
    if (typeface == nullptr) {
      continue;
    }

    const uint32_t typefaceID = typeface->uniqueID();
    if (fontHeightMap.find(typefaceID) == fontHeightMap.end()) {
      auto font = _font;
      font.setTypeface(typeface);
      const auto& fontMetrics = font.getMetrics();
      fontHeightMap[typefaceID] = std::fabs(fontMetrics.ascent) + std::fabs(fontMetrics.descent) +
                                  std::fabs(fontMetrics.leading);
    }

    lineHeight = std::max(lineHeight, fontHeightMap[typefaceID]);
  }

  return lineHeight;
}

void TextLayer::TruncateGlyphLines(std::vector<std::shared_ptr<GlyphLine>>& glyphLines) const {
  if (_height == 0.0f || glyphLines.empty()) {
    return;
  }

  float totalLineHeight = 0.0f;
  for (auto item = glyphLines.begin(); item != glyphLines.end(); ++item) {
    const auto& glyphLine = *item;
    const float lineHeight = getLineHeight(glyphLine);

    // Ensure at least one line is kept
    if (item == glyphLines.begin()) {
      totalLineHeight += lineHeight;
      continue;
    }

    if (totalLineHeight + lineHeight > _height) {
      glyphLines.erase(item, glyphLines.end());
      break;
    }

    totalLineHeight += lineHeight;
  }
}

std::vector<std::shared_ptr<TextLayer::GlyphInfo>> TextLayer::ShapeText(
    const std::string& text, const std::shared_ptr<Typeface>& typeface) {
  if (text.empty()) {
    return {};
  }

  const char* head = text.data();
  const char* tail = head + text.size();
  auto fallbackTypefaces = GetFallbackTypefaces();
  std::vector<std::shared_ptr<GlyphInfo>> glyphInfos;
  glyphInfos.reserve(text.size());

  while (head < tail) {
    const auto characterUnicode = UTF::NextUTF8(&head, tail);
    if ('\n' == characterUnicode) {
      const GlyphID lineFeedCharacterGlyphID = nullptr != typeface ? typeface->getGlyphID('\n') : 0;
      glyphInfos.emplace_back(
          std::make_shared<GlyphInfo>('\n', lineFeedCharacterGlyphID, typeface));
    } else {
      GlyphID glyphID = typeface ? typeface->getGlyphID(characterUnicode) : 0;
      if (glyphID <= 0) {
        for (const auto& fallbackTypeface : fallbackTypefaces) {
          if (nullptr != fallbackTypeface) {
            glyphID = fallbackTypeface->getGlyphID(characterUnicode);
            if (glyphID > 0) {
              glyphInfos.emplace_back(
                  std::make_shared<GlyphInfo>(characterUnicode, glyphID, fallbackTypeface));
              break;
            }
          }
        }

        // If the glyph is still not found, use the space character.
        if (glyphID <= 0) {
          glyphInfos.emplace_back(std::make_shared<GlyphInfo>(' ', 0, typeface));
        }
      } else {
        glyphInfos.emplace_back(std::make_shared<GlyphInfo>(characterUnicode, glyphID, typeface));
      }
    }
  }

  return glyphInfos;
}

void TextLayer::resolveTextAlignment(const std::vector<std::shared_ptr<GlyphLine>>& glyphLines,
                                     float emptyAdvance,
                                     std::vector<std::shared_ptr<GlyphInfo>>& finalGlyphInfos,
                                     std::vector<Point>& positions) const {
  if (glyphLines.empty()) {
    return;
  }

  float xOffset = 0.0f;
  float yOffset = 0.0f;
  for (size_t i = 0; i < glyphLines.size(); ++i) {
    const auto& glyphLine = glyphLines[i];
    const auto lineGlyphCount = glyphLine->getGlyphCount();

    const auto lineWidth = glyphLine->getLineWidth();
    const auto lineHeight = getLineHeight(glyphLine);

    xOffset = 0.0f;
    yOffset += lineHeight;
    float spaceWidth = 0.0f;

    if (_width != 0.0f) {
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
          // 1. Each line must have more than one character
          if (lineGlyphCount <= 1) {
            break;
          }

          // 2. If auto-wrap is disabled and the width is set very small, spaceWidth might be negative, causing characters to overlap. This needs to be avoided.
          if (_width < lineWidth) {
            break;
          }

          // 3. The last line should not be justified (align it to the left), or if auto-wrap is disabled and there is only one line of text, it should be justified.
          if (i < glyphLines.size() - 1 || (!_autoWrap && 1 == glyphLines.size())) {
            spaceWidth = (_width - lineWidth) / static_cast<float>(lineGlyphCount - 1);
          }
          break;
        }
        default:
          break;
      }
    }

    for (size_t i = 0; i < lineGlyphCount; ++i) {
      const auto& glyphInfo = glyphLine->getGlyphInfo(i);
      if (glyphInfo == nullptr) {
        continue;
      }

      if (glyphInfo->getGlyphID() <= 0) {
        xOffset += emptyAdvance;
        continue;
      }

      auto point = Point();
      if (lineGlyphCount > 1) {
        point.x += xOffset + spaceWidth * static_cast<float>(i);
      } else {
        point.x += xOffset;
      }
      point.y = yOffset;

      finalGlyphInfos.emplace_back(std::move(glyphInfo));
      positions.emplace_back(point);

      xOffset += glyphLine->getAdvance(i);
    }
  }
}

void TextLayer::buildGlyphRunList(const std::vector<std::shared_ptr<GlyphInfo>>& finalGlyphs,
                                  const std::vector<Point>& positions,
                                  std::vector<GlyphRun>& glyphRunList) const {
  std::unordered_map<uint32_t, GlyphRun> glyphRunMap = {};
  for (size_t i = 0; i < finalGlyphs.size(); ++i) {
    const auto& glyphInfo = finalGlyphs[i];
    if (glyphInfo == nullptr) {
      continue;
    }

    const auto& typeface = glyphInfo->getTypeface();
    if (typeface == nullptr) {
      continue;
    }

    auto typefaceID = typeface->uniqueID();
    if (glyphRunMap.find(typefaceID) == glyphRunMap.end()) {
      auto font = _font;
      font.setTypeface(typeface);
      glyphRunMap[typefaceID] = GlyphRun(font, {}, {});
    }
    auto& fontGlyphRun = glyphRunMap[typefaceID];
    fontGlyphRun.glyphs.emplace_back(glyphInfo->getGlyphID());
    fontGlyphRun.positions.push_back(std::move(positions[i]));
  }

  for (const auto& fontGlyphRun : glyphRunMap) {
    glyphRunList.emplace_back(std::move(fontGlyphRun.second));
  }
}
}  // namespace tgfx
