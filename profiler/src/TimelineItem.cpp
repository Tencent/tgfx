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

#include "TimelineItem.h"
#include "src/profiler/IconsFontAwesome6.h"
#include "view/TimelineView.h"

TimelineItem::TimelineItem(TimelineView& view, tracy::Worker& worker)
  : height(0)
  , visible(true)
  , showFull(true)
  , worker(worker)
  , timelineView(view)
{

}

void TimelineItem::adjustThreadHeight(bool firstFrame, int yBegin, int yEnd) {

  const auto speed = 4.0;
  const auto baseMove = 1.0;

  const auto newHeight = yEnd - yBegin;
  if(firstFrame)
  {
    height = newHeight;
  }
  else if(height != newHeight)
  {
    const auto diff = newHeight - height;
    // const auto preClampMove = diff * speed * ImGui::GetIO().DeltaTime;
    const auto preClampMove = diff * speed * 1;
    if(diff > 0)
    {
      const auto move = preClampMove + baseMove;
      height = int(std::min<double>(height + move, newHeight));
    }
    else
    {
      const auto move = preClampMove - baseMove;
      height = int(std::max<double>(height + move, newHeight));
    }
  }
}

void TimelineItem::draw(bool firstFrame, const TimelineContext ctx, int yOffset, tgfx::Canvas* canvas, const AppHost* appHost) {
  const auto yBegin = yOffset;
  auto yEnd = yOffset;

  if (!isVisible()) {
    drawFinished();
    if (height != 0) {
      adjustThreadHeight(firstFrame, yBegin, yEnd);
    }
    return;
  }

  if (isEmpty()) {
    drawFinished();
    return;
  }

  const auto w = ctx.w;
  const auto ty = ctx.ty;
  const auto ostep = ty + 1;
  const auto& wpos = ctx.wpos;
  const auto yPos = wpos.y + yBegin;
  const auto dpos = wpos + tgfx::Point{0.5f, 0.5f};

  yEnd += static_cast<int>(ostep);
  if (showFull) {
    if (!drawContent(ctx, yEnd, canvas) && !timelineView.getViewData()->drawEmptyLabels) {
      drawFinished();
      yEnd = yBegin;
      adjustThreadHeight(firstFrame, yBegin,yEnd);
      return;
    }
  }

  drawOverlay(wpos + tgfx::Point{0.f, static_cast<float>(yBegin)}, wpos + tgfx::Point{w, static_cast<float>(yEnd)});

  const auto hdrOffset = yBegin;
  const bool drawHeader = yPos + ty >= ctx.yMin && yPos <= ctx.yMax;
  if (drawHeader) {
    const auto color = headerColor();
    const auto colorInactive = headerColorInactive();
    if (showFull) {
      tgfx::Point point = wpos + tgfx::Point{0.f, hdrOffset + ty};
      drawText(canvas, appHost, ICON_FA_CARET_DOWN, point.x, point.y, color);
    }
    else {
      tgfx::Point point = wpos + tgfx::Point{0, hdrOffset + ty};
      drawText(canvas, appHost, ICON_FA_CARET_RIGHT, point.x, point.y, color);
    }

    const auto lable = headerLable();
    tgfx::Point point = wpos + tgfx::Point{ty, hdrOffset + ty};
    drawText(canvas, appHost, lable, point.x, point.y, showFull ? color : colorInactive);
    if (showFull) {
      tgfx::Point p1 = dpos + tgfx::Point{0, hdrOffset + ty + 1};
      tgfx::Point p2 = dpos + tgfx::Point{w, hdrOffset + ty + 1};
      drawLine(canvas, p1, p2, headlineColor());
    }
  }

  if (ctx.hover) {
    // TODO hove event
  }

  yEnd += static_cast<int>(0.2f * ostep);
  adjustThreadHeight(firstFrame, yBegin, yEnd);
  drawFinished();
}
