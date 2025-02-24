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

#include <QPainter>
#include "tgfx/core/Canvas.h"
#include "TracyTaskDispatch.hpp"
#include "TimelineContext.h"
#include "TracyWorker.hpp"
#include "Utility.h"

class TimelineView;

class TimelineItem {
public:
  TimelineItem(TimelineView& view, tracy::Worker& worker);
  virtual ~TimelineItem() = default;

  void draw(bool firstFrame, const TimelineContext ctx, int yOffset, tgfx::Canvas* canvas, const AppHost* appHost);
  virtual void preprocess(const TimelineContext& ctx, tracy::TaskDispatch& td, bool visible) = 0;
  virtual uint32_t headerColor() const = 0;
  virtual uint32_t headerColorInactive() const = 0;
  virtual uint32_t headlineColor() const = 0;
  virtual const char* headerLable() const = 0;

  virtual void setVisible(bool v) { visible = v; }
  virtual bool isVisible() { return visible; }
  virtual bool isEmpty() { return false; }
  virtual void drawOverlay(const tgfx::Point&, const tgfx::Point&) {}

  int getHeight() const {return height;}
protected:
  virtual bool drawContent(const TimelineContext& ctx, int& offset, tgfx::Canvas* painter) = 0;
  virtual void drawFinished() {}
  void adjustThreadHeight(bool firstFrame, int yBegin, int yEnd);
private:
  int height;
protected:
  bool visible;
  bool showFull;
  tracy::Worker& worker;
  TimelineView& timelineView;
};