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

#include "Utility.h"
#include <QFontMetrics>
#include <filesystem>
#include "tgfx/core/Paint.h"
#include "tgfx/layers/TextLayer.h"

// Short list based on GetTypes() in TracySourceTokenizer.cpp
constexpr const char* TypesList[] = {
    "bool ",     "char ",     "double ",   "float ",     "int ",      "long ",
    "short ",    "signed ",   "unsigned ", "void ",      "wchar_t ",  "size_t ",
    "int8_t ",   "int16_t ",  "int32_t ",  "int64_t ",   "intptr_t ", "uint8_t ",
    "uint16_t ", "uint32_t ", "uint64_t ", "ptrdiff_t ", nullptr};

AppHost::AppHost(int width, int height, float density)
    : _width(width), _height(height), _density(density) {
}

void AppHost::addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface) {
  if (name.empty()) {
    return;
  }
  if (typeface == nullptr) {
    return;
  }
  if (typefaces.count(name) > 0) {
    return;
  }
  typefaces[name] = std::move(typeface);
}

std::shared_ptr<tgfx::Typeface> AppHost::getTypeface(const std::string& name) const {
  auto result = typefaces.find(name);
  if (result != typefaces.end()) {
    return result->second;
  }
  return nullptr;
}

bool AppHost::updateScreen(int width, int height, float density) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (density < 1.0) {
    return false;
  }
  if (width == _width && height == _height && density == _density) {
    return false;
  }
  _width = width;
  _height = height;
  _density = density;
  return true;
}

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

uint32_t getThreadColor(uint64_t thread, int depth, bool dynamic) {
  if (!dynamic) return 0xFFCC5555;
  return tracy::GetHsvColor(thread, depth);
}

tgfx::Rect getTextSize(const AppHost* appHost, const char* text, size_t textSize) {
  std::string strText(text);
  auto iter = TextSizeMap.find(strText);
  if (iter != TextSizeMap.end()) {
    return iter->second;
  }
  if (textSize) {
    strText = std::string(text, textSize);
  }
  auto typeface = appHost->getTypeface("default");
  tgfx::Font font(typeface, FontSize);
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
              float y, uint32_t color) {
  tgfx::Paint paint;
  paint.setColor(getTgfxColor(color));
  auto typeface = appHost->getTypeface("default");
  tgfx::Font font(typeface, FontSize);
  canvas->drawSimpleText(text, x, y, font, paint);
}

void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, tgfx::Point pos, uint32_t color,
                      const char* text) {
  drawTextContrast(canvas, appHost, pos.x, pos.y, color, text);
}

void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, float x, float y,
                      uint32_t color, const char* text) {
  auto height = abs(getTextSize(appHost, text).top) + 1;
  drawText(canvas, appHost, text, x + 0.5f, y + height + 0.5f, 0xAA000000);
  drawText(canvas, appHost, text, x, y + height, color);
}

const char* shortenZoneName(const AppHost* appHost, ShortenName type, const char* name,
                            tgfx::Rect tsz, float zsz) {
  assert(type != ShortenName::Never);
  if (name[0] == '<' || name[0] == '[') return name;
  if (type == ShortenName::Always) zsz = 0;

  static char buf[64 * 1024];
  char tmp[64 * 1024];

  auto end = name + strlen(name);
  auto ptr = name;
  auto dst = tmp;
  int cnt = 0;
  for (;;) {
    auto start = ptr;
    while (ptr < end && *ptr != '<') ptr++;
    memcpy(dst, start, size_t(ptr - start + 1));
    dst += ptr - start + 1;
    if (ptr == end) break;
    cnt++;
    ptr++;
    while (cnt > 0) {
      if (ptr == end) break;
      if (*ptr == '<') cnt++;
      else if (*ptr == '>')
        cnt--;
      ptr++;
    }
    *dst++ = '>';
  }

  end = dst - 1;
  ptr = tmp;
  dst = buf;
  cnt = 0;
  for (;;) {
    auto start = ptr;
    while (ptr < end && *ptr != '(') ptr++;
    memcpy(dst, start, size_t(ptr - start + 1));
    dst += ptr - start + 1;
    if (ptr == end) break;
    cnt++;
    ptr++;
    while (cnt > 0) {
      if (ptr == end) break;
      if (*ptr == '(') cnt++;
      else if (*ptr == ')')
        cnt--;
      ptr++;
    }
    *dst++ = ')';
  }

  end = dst - 1;
  if (end - buf > 6 && memcmp(end - 6, " const", 6) == 0) {
    dst[-7] = '\0';
    end -= 6;
  }

  ptr = buf;
  for (;;) {
    auto match = TypesList;
    while (*match) {
      auto m = *match;
      auto p = ptr;
      while (*m) {
        if (*m != *p) break;
        m++;
        p++;
      }
      if (!*m) {
        ptr = p;
        break;
      }
      match++;
    }
    if (!*match) break;
  }

  tsz = getTextSize(appHost, ptr, static_cast<size_t>(end - ptr));

  if (type == ShortenName::OnlyNormalize || tsz.width() < zsz) return ptr;

  for (;;) {
    auto p = ptr;
    while (p < end && *p != ':') p++;
    if (p == end) return ptr;
    p++;
    while (p < end && *p == ':') p++;
    ptr = p;
    tsz = getTextSize(appHost, ptr, static_cast<size_t>(end - ptr));
    if (tsz.width() < zsz) return ptr;
  }
}