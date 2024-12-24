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

#include "TimelineItem.h"
#include "TimelineView.h"
#include "src/profiler/IconsFontAwesome6.h"

TimelineItem::TimelineItem(TimelineView& view, tracy::Worker& worker, const void* key)
  : key(key)
  , height(0)
  , visible(true)
  , showFull(true)
  , timelineView(view)
  , worker(worker)
{

}

void TimelineItem::adjustThreadHeight(bool firstFrame, int yBegin, int yEnd) {

  const auto speed = 4.0;
  const auto baseMove = 1.0;

  const auto newHeight = yEnd - yBegin;
  if( firstFrame )
  {
    height = newHeight;
  }
  else if( height != newHeight )
  {
    const auto diff = newHeight - height;
    // const auto preClampMove = diff * speed * ImGui::GetIO().DeltaTime;
    const auto preClampMove = diff * speed * 1;
    if( diff > 0 )
    {
      const auto move = preClampMove + baseMove;
      height = int( std::min<double>( height + move, newHeight ) );
    }
    else
    {
      const auto move = preClampMove - baseMove;
      height = int( std::max<double>( height + move, newHeight ) );
    }
  }
}

void TimelineItem::draw(bool firstFrame, const TimelineContext ctx, int yOffset, QPainter* painter) {
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
  const auto yPos = wpos.y() + yBegin;
  const auto dpos = wpos + QPointF( 0.5f, 0.5f );

  yEnd += ostep;
  if (showFull) {
    if (!drawContent(ctx, yEnd, painter) && !timelineView.getViewData().drawEmptyLabels) {
      drawFinished();
      yEnd = yBegin;
      adjustThreadHeight(firstFrame, yBegin,yEnd);
      return;
    }
  }

  drawOverlay(wpos + QPointF(0, yBegin), wpos + QPointF(w, yEnd));

  float lableWidth;
  const auto hdrOffset = yBegin;
  const bool drawHeader = yPos + ty >= ctx.yMin && yPos <= ctx.yMax;
  if (drawHeader) {
    const auto color = headerColor();
    const auto colorInactive = headerColorInactive();
    if (showFull) {
      painter->setPen(getColor(color));
      painter->drawText(wpos + QPointF(0, hdrOffset + ty), ICON_FA_CARET_DOWN);
    }
    else {
      painter->setPen(getColor(colorInactive));
      painter->drawText(wpos + QPointF(0, hdrOffset + ty), ICON_FA_CARET_RIGHT);
    }

    const auto lable = headerLable();
    lableWidth = getFontSize(lable).width();
    painter->setPen(getColor(showFull ? color : colorInactive));
    painter->drawText(wpos + QPointF(ty, hdrOffset + ty), lable);
    if (showFull) {
      painter->setPen(getColor(headlineColor()));
      painter->drawLine(dpos + QPointF(0, hdrOffset + ty + 1), dpos + QPointF(w, hdrOffset + ty + 1));
    }
  }

  if (ctx.hover) {
    // TODO hove event
  }

  yEnd += 0.2f * ostep;
  adjustThreadHeight(firstFrame, yBegin, yEnd);
  drawFinished();
}
