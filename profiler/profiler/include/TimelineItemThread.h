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
#include <QGraphicsView>
#include <src/profiler/TracyTimelineDraw.hpp>
#include "TimelineItem.h"

class TimelineItemThread: public TimelineItem {
public:
  TimelineItemThread(TimelineView& view, tracy::Worker& worker, const tracy::ThreadData* key);

  void preprocess(const TimelineContext& ctx, tracy::TaskDispatch& td, bool visible, int yPos) override;

  uint32_t headerColor() const override;
  uint32_t headerColorInactive() const override;
  uint32_t headlineColor() const override;
  const char* headerLable() const override;
protected:
  void drawFinished() override;
  bool drawContent(const TimelineContext& ctx, int& offset, QPainter* painter) override;

private:
  int preprocessZoneLevel(const TimelineContext& ctx, const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& vec,
      int depth, bool visible, const uint32_t inheritedColor);

  template<typename Adapter, typename V>
  int preprocessZoneLevel(const TimelineContext& ctx, const V& vec, int depth, bool visible, const uint32_t inheritedColor);

  const tracy::ThreadData* threadData;
  std::vector<tracy::TimelineDraw> draws;
  int depth;
  bool showFull;
  bool hasSamples;
  bool hasMessage;
private:

};