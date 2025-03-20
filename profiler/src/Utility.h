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
#include <QColor>
#include <iostream>
#include "AppHost.h"
#include "TracyEvent.hpp"
#include "src/profiler/TracyColor.hpp"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"

static std::unordered_map<std::string, tgfx::Rect> TextSizeMap;
const auto FontSize = 15;
const auto MaxHeight = 28;
const auto zoneMargin = 1.f;

enum class ProfilerStatus { Connect, ReadFile, None };

enum ViewMode { Paused, LastFrames, LastRange };

enum class ShortenName : uint8_t {
  Never,
  Always,
  OnlyNormalize,
  NoSpace,
  NoSpaceAndNormalize,
};

struct Config {
  bool threadedRendering = true;
  bool focusLostLimit = true;
  int targetFps = 60;
  double horizontalScrollMultiplier = 1.0;
  double verticalScrollMultiplier = 1.0;
  bool memoryLimit = false;
  size_t memoryLimitPercent = 80;
  bool achievements = false;
  bool achievementsAsked = false;
  int dynamicColors = 1;
  bool forceColors = false;
  int shortenName = (int)ShortenName::NoSpaceAndNormalize;
};

tgfx::Color getTgfxColor(uint32_t color);

tgfx::Rect getTextSize(const AppHost* appHost, const char* text, size_t textSize = 0);
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
              float y, uint32_t color);
void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, float x, float y,
                      uint32_t color, const char* text);
void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, tgfx::Point pos, uint32_t color,
                      const char* text);

uint32_t getThreadColor(uint64_t thread, int depth, bool dynamic);
const char* shortenZoneName(const AppHost* appHost, ShortenName type, const char* name,
                            tgfx::Rect tsz, float zsz);