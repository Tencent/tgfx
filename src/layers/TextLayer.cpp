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
#include "tgfx/core/UTF.h"

namespace tgfx {

#define TGFX_DEFAULT_TEXT_LAYER_LINE_HEIGHT 1.2f

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

void TextLayer::onDraw(Canvas* canvas, float alpha) {
  if (_text.empty()) {
    return;
  }
  Paint paint;
  paint.setAntiAlias(allowsEdgeAntialiasing());
  auto currentColor = _textColor;
  currentColor.alpha *= alpha;
  paint.setColor(currentColor);
  paint.setStyle(tgfx::PaintStyle::Fill);
  auto glyphRun = createGlyphRun();
  canvas->drawGlyphs(glyphRun.glyphIDs().data(), glyphRun.positions().data(), glyphRun.runSize(),
                     glyphRun.font(), paint);
}

Rect TextLayer::measureContentBounds() const {
  if (_text.empty()) {
    return Rect::MakeEmpty();
  }
  auto glyphRun = createGlyphRun();
  auto bounds = glyphRun.getBounds(Matrix::I());
  return Rect::MakeWH(bounds.right, bounds.bottom);
}

GlyphRun TextLayer::createGlyphRun() const {
  if (_text.empty()) {
    return {};
  }
  std::vector<GlyphID> glyphs = {};
  std::vector<Point> positions = {};
  const char* textStart = _text.data();
  const char* textStop = textStart + _text.size();
  auto emptyGlyphID = _font.getGlyphID(" ");
  auto emptyAdvance = _font.getAdvance(emptyGlyphID);
  auto xGlyphID = _font.getGlyphID("x");
  auto xHeight = 0.f;
  if (xGlyphID > 0) {
    xHeight = _font.getBounds(xGlyphID).y();
  }
  auto lineHeight = ceil(_font.getSize() * TGFX_DEFAULT_TEXT_LAYER_LINE_HEIGHT);
  auto baseLine = lineHeight / 2 - xHeight / 2;
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
  return GlyphRun(_font, glyphs, positions);
}

}  // namespace tgfx
