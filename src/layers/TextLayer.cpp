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
#include "core/utils/SimpleTextShaper.h"
#include "layers/contents/TextContent.h"
#include "tgfx/core/UTF.h"

namespace tgfx {

static constexpr float DefaultLineHeight = 1.2f;
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
      xOffset = -emptyAdvance;
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
  return std::make_unique<TextContent>(std::move(glyphRun), _textColor);
}
}  // namespace tgfx
