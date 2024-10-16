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

namespace tgfx {
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
  paint.setAlpha(alpha);
  auto currentColor = _textColor;
  currentColor.alpha *= paint.getAlpha();
  paint.setColor(currentColor);
  paint.setStyle(tgfx::PaintStyle::Fill);
  canvas->drawSimpleText(_text, 0, _font.getSize(), _font, paint);
}

}  // namespace tgfx
