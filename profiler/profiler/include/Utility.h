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
#include <QFont>
#include <QRect>
#include <QColor>
#include <QPainter>
#include "TracyEvent.hpp"
class Worker;

enum class ShortenName : uint8_t
{
  Never,
  Always,
  OnlyNormalize,
  NoSpace,
  NoSpaceAndNormalize,
};

QFont& getFont();
QRect getFontSize(const char* text, size_t textSize = 0);
QColor getColor(uint32_t color);

void drawPolyLine(QPainter* painter, QPointF p1, QPointF p2, QPointF p3, QColor color, float thickness = 1.f);
void drawTextContrast(QPainter* painter, QPointF pos, QColor color, const char* test);

uint32_t getThreadColor( uint64_t thread, int depth, bool dynamic );
uint32_t getPlotColor( const tracy::PlotData& plot, const Worker& worker );
const char* shortenZoneName(ShortenName type, const char* name, QRect tsz, float zsz);