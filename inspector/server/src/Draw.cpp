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

#include "Draw.h"
#include <QFontMetrics>
#include <filesystem>
#include "tgfx/core/Paint.h"
#include "tgfx/layers/TextLayer.h"

namespace inspector {
// Short list based on GetTypes() in TracySourceTokenizer.cpp
bool isEqual(float num1, float num2) {
  return abs(num1 - num2) < 0.00001;
}

tgfx::Color getTgfxColor(uint32_t color) {
  uint8_t r = (color) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = (color >> 16) & 0xFF;
  uint8_t a = (color >> 24) & 0xFF;
  return tgfx::Color::FromRGBA(r, g, b, a);
}

tgfx::Rect getTextSize(const AppHost* appHost, const char* text, size_t textSize, float fontSize) {
  std::string strText(text);
  auto iter = TextSizeMap.find(strText);
  if (iter != TextSizeMap.end()) {
    return iter->second;
  }
  if (textSize) {
    strText = std::string(text, textSize);
  }
  auto typeface = appHost->getTypeface("default");
  tgfx::Font font(typeface, fontSize);
  auto textBlob = tgfx::TextBlob::MakeFrom(strText, font);
  auto rect = textBlob->getBounds();
  TextSizeMap.emplace(strText, rect);
  return rect;
}

void drawPath(tgfx::Canvas* canvas, tgfx::Path& path, uint32_t color, float thickness) {
  tgfx::Paint paint;
  paint.setColor(getTgfxColor(color));
  if (thickness > 0.f) {
    paint.setStyle(tgfx::PaintStyle::Stroke);
    paint.setStrokeWidth(thickness);
  } else {
    paint.setStyle(tgfx::PaintStyle::Fill);
  }
  canvas->drawPath(path, paint);
}

void drawRect(tgfx::Canvas* canvas, float x0, float y0, float w, float h, uint32_t color,
              float thickness) {
  tgfx::Rect rect = tgfx::Rect::MakeXYWH(x0, y0, w, h);
  drawRect(canvas, rect, color, thickness);
}

void drawRect(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, uint32_t color,
              float thickness) {
  tgfx::Rect rect = tgfx::Rect::MakeXYWH(p1.x, p1.y, p2.x, p2.y);
  drawRect(canvas, rect, color, thickness);
}

void drawRect(tgfx::Canvas* canvas, tgfx::Rect& rect, uint32_t color, float thickness) {
  tgfx::Paint paint;
  paint.setColor(getTgfxColor(color));
  if (thickness > 0.f) {
    paint.setStyle(tgfx::PaintStyle::Stroke);
    paint.setStrokeWidth(thickness);
  } else {
    paint.setStyle(tgfx::PaintStyle::Fill);
  }
  canvas->drawRect(rect, paint);
}

void drawLine(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, tgfx::Point& p3,
              uint32_t color, float thickness) {
  tgfx::Paint paint;
  paint.setStroke(tgfx::Stroke(thickness));
  paint.setStyle(tgfx::PaintStyle::Stroke);
  paint.setColor(getTgfxColor(color));
  tgfx::Path path;
  path.moveTo(p1);
  path.lineTo(p2);
  path.lineTo(p3);
  canvas->drawPath(path, paint);
}

void drawLine(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, uint32_t color) {
  drawLine(canvas, p1.x, p1.y, p2.x, p2.y, color);
}

void drawLine(tgfx::Canvas* canvas, float x0, float y0, float x1, float y1, uint32_t color) {
  if (isEqual(x0, x1) && isEqual(y0, y1)) {
    return;
  }
  tgfx::Paint paint;
  paint.setColor(getTgfxColor(color));
  canvas->drawLine(x0, y0, x1, y1, paint);
}

void drawText(tgfx::Canvas* canvas, const AppHost* appHost, const std::string& text, float x,
              float y, uint32_t color, float fontSize) {
  tgfx::Paint paint;
  paint.setColor(getTgfxColor(color));
  auto typeface = appHost->getTypeface("default");
  tgfx::Font font(typeface, fontSize);
  canvas->drawSimpleText(text, x, y, font, paint);
}

void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, tgfx::Point pos, uint32_t color,
                      const char* text, float fontSize) {
  drawTextContrast(canvas, appHost, pos.x, pos.y, color, text, fontSize);
}

void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, float x, float y,
                      uint32_t color, const char* text, float fontSize) {
  auto height = abs(getTextSize(appHost, text).top) + 1;
  drawText(canvas, appHost, text, x + 0.5f, y + height + 0.5f, 0xAA000000, fontSize);
  drawText(canvas, appHost, text, x, y + height, color, fontSize);
}

void drawTextWithBlackRect(tgfx::Canvas* canvas, const AppHost* appHost, const char* text, float x,
                           float y, uint32_t color, float fontSize) {
  auto textBounds = getTextSize(appHost, text);
  drawRect(canvas, x, y - textBounds.height(), textBounds.width(), textBounds.height(), 0xFF000000);
  drawText(canvas, appHost, text, x + 1, y - 1.5f, color, fontSize);
}
}  // namespace inspector