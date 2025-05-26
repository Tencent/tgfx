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

#pragma once
#include "AppHost.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"

namespace inspector {
static std::unordered_map<std::string, tgfx::Rect> TextSizeMap;
const auto FontSize = 15.f;
const auto MaxHeight = 28;
const auto zoneMargin = 1.f;

tgfx::Color getTgfxColor(uint32_t color);

tgfx::Rect getTextSize(const AppHost* appHost, const char* text, size_t textSize = 0,
                       float fontSize = FontSize);
void drawPath(tgfx::Canvas* canvas, tgfx::Path& path, uint32_t color, float thickness = 0.f);
void drawRect(tgfx::Canvas* canvas, float x0, float y0, float w, float h, uint32_t color,
              float thickness = 0.f);
void drawRect(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, uint32_t color,
              float thickness = 0.f);
void drawRect(tgfx::Canvas* canvas, tgfx::Rect& rect, uint32_t color, float thickness = 0.f);
void drawLine(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, uint32_t color);
void drawLine(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, tgfx::Point& p3,
              uint32_t color, float thickness = 1.f);
void drawLine(tgfx::Canvas* canvas, float x0, float y0, float x1, float y1, uint32_t color);
void drawText(tgfx::Canvas* canvas, const AppHost* appHost, const std::string& text, float x,
              float y, uint32_t color, float fontSize = FontSize);
void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, float x, float y,
                      uint32_t color, const char* text, float fontSize = FontSize);
void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, tgfx::Point pos, uint32_t color,
                      const char* text, float fontSize = FontSize);
void drawTextWithBlackRect(tgfx::Canvas* canvas, const AppHost* appHost, const char* text, float x,
                           float y, uint32_t color, float fontSize = FontSize);
}  // namespace inspector