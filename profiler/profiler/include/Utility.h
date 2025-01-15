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

#pragma once
#include <stdint.h>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <iostream>

#include "TracyEvent.hpp"

#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"

static std::unordered_map<std::string, tgfx::Rect> TextSizeMap;

enum class ProfilerStatus {
  Connect,
  ReadFile,
  None
};

class AppHost {
public:
  explicit AppHost(int width = 1280, int height = 720, float density = 1.0f);

  int width() const { return _width; }
  int height() const { return _height; }
  float density() const { return _density; }

  void addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface);
  std::shared_ptr<tgfx::Typeface> getTypeface(const std::string& name) const;
  bool updateScreen(int width, int height, float density);
private:
  int _width = 1280;
  int _height = 720;
  float _density = 1.0f;
  std::unordered_map<std::string, std::shared_ptr<tgfx::Typeface>> typefaces = {};
};

enum class ShortenName : uint8_t
{
  Never,
  Always,
  OnlyNormalize,
  NoSpace,
  NoSpaceAndNormalize,
};

class TestTime {
public:
  TestTime(const char* name) {
    start = std::chrono::high_resolution_clock::now();
    this->name = name;
  }
  ~TestTime() {
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << name << "当前花费时间: " << elapsed.count() << " second" << std::endl;
  }
private:
  std::chrono::time_point<std::chrono::steady_clock> start;
  const char* name;
};

tgfx::Color getTgfxColor(uint32_t color);

tgfx::Rect getTextSize(const AppHost* appHost, const char* text, size_t textSize = 0);
void drawRect(tgfx::Canvas* canvas, float x0, float y0, float w, float h, uint32_t color);
void drawRect(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, uint32_t color);
void drawRect(tgfx::Canvas* canvas, tgfx::Rect& rect, uint32_t color);
void drawLine(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, uint32_t color);
void drawLine(tgfx::Canvas* canvas, tgfx::Point& p1, tgfx::Point& p2, tgfx::Point& p3, uint32_t color, float thickness = 1.f);
void drawLine(tgfx::Canvas* canvas, float x0, float y0, float x1, float y1, uint32_t color);
void drawText(tgfx::Canvas* canvas, const AppHost* appHost, const std::string& text, float x, float y, uint32_t color);
void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, float x, float y, uint32_t color, const char* text);
void drawTextContrast(tgfx::Canvas* canvas, const AppHost* appHost, tgfx::Point pos, uint32_t color, const char* text);

uint32_t getThreadColor( uint64_t thread, int depth, bool dynamic );
const char* shortenZoneName(const AppHost* appHost, ShortenName type, const char* name, tgfx::Rect tsz, float zsz);