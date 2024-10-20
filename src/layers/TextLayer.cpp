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

std::shared_ptr<TextLayer> TextLayer::Make() {
  auto layer = std::shared_ptr<TextLayer>(new TextLayer());
  layer->weakThis = layer;
  return layer;
}

void TextLayer::setText(const std::string& text) {
  _text = text;
  invalidateContent();
}

void TextLayer::setTextColor(const Color& color) {
  _textColor = color;
  invalidateContent();
}

void TextLayer::setFont(const Font& font) {
  _font = font;
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
  return std::make_unique<TextContent>(GlyphRun{_font, std::move(glyphs), std::move(positions)});
}

void TextLayer::onUpdatePaint(Paint* paint) {
  auto color = _textColor;
  color.alpha *= paint->getAlpha();
  paint->setColor(color);
}

}  // namespace tgfx
