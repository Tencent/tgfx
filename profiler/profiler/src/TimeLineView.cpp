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

#include "TimelineView.h"
#include <qevent.h>
#include <src/profiler/TracyColor.hpp>
#include "TimelineItemThread.h"

constexpr float MinVisSize = 3;

TimelineView::TimelineView(tracy::Worker& worker, ViewData& viewData, bool threadedRendering, QWidget* parent)
  : worker(worker)
  , viewData(viewData)
  , timelineController(*this, worker, threadedRendering)
  , QGraphicsView(parent) {

  // QGraphicsScene *scene = new QGraphicsScene(this);
  // scene->setItemIndexMethod(QGraphicsScene::NoIndex);
  // // scene->setSceneRect(0, 0, width, height);
  // setScene(scene);
  // setMouseTracking(true);
  setViewportUpdateMode(BoundingRectViewportUpdate);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // setStyleSheet("background-color: rgb(255, 0, 0)");
}

TimelineView::~TimelineView() {

}


uint32_t TimelineView::getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth) {
  auto namehash = srcloc.namehash;
  if( namehash == 0 && srcloc.function.active )
  {
    const auto f = worker.GetString( srcloc.function );
    namehash = tracy::charutil::hash( f );
    if( namehash == 0 ) namehash++;
    srcloc.namehash = namehash;
  }
  if( namehash == 0 )
  {
    return tracy::GetHsvColor( uint64_t( &srcloc ), depth );
  }
  else
  {
    return tracy::GetHsvColor( namehash, depth );
  }
}

uint32_t TimelineView::getZoneColor(const tracy::ZoneEvent& ev, uint64_t thread, int depth) {
  const auto sl = ev.SrcLoc();
  const auto& srcloc = worker.GetSourceLocation( sl );
  if( !viewData.forceColors )
  {
    if( worker.HasZoneExtra( ev ) )
    {
      const auto custom_color = worker.GetZoneExtra( ev ).color.Val();
      if( custom_color != 0 ) return custom_color | 0xFF000000;
    }
    const auto color = srcloc.color;
    if( color != 0 ) return color | 0xFF000000;
  }
  switch( viewData.dynamicColors )
  {
    case 0:
      return 0xFFCC5555;
    case 1:
      return tracy::GetHsvColor( thread, depth );
    case 2:
      return getRawSrcLocColor( srcloc, depth );
    default:
      assert( false );
    return 0;
  }
}

TimelineView::ZoneColorData TimelineView::getZoneColorData(const tracy::ZoneEvent& ev, uint64_t thread, int depth, uint32_t inheritedColor) {
  ZoneColorData ret;
  const auto& srcloc = ev.SrcLoc();

  const auto color = inheritedColor ? inheritedColor : getZoneColor( ev, thread, depth );
  ret.color = color;
  ret.accentColor = tracy::HighlightColor( color );
  ret.thickness = 1.f;
  ret.highlight = false;
  return ret;
}

void TimelineView::drawMouseLine(QPainter* painter) {

}

void TimelineView::drawTimelineFrames(QPainter* painter) {

}

void TimelineView::drawZonelist(const TimelineContext& ctx, const std::vector<tracy::TimelineDraw>& drawList,
  int _offset, uint64_t tid, QPainter* painter) {

  const auto w = ctx.w;
  const auto& wpos = ctx.wpos;
  const auto dpos = wpos + QPointF( 0.5f, 0.5f );
  const auto ty = ctx.ty;
  const auto ostep = ty + 1;
  const auto yMin = ctx.yMin;
  const auto yMax = ctx.yMax;
  const auto pxns = ctx.pxns;
  const auto hover = ctx.hover;
  const auto vStart = ctx.vStart;

  for(auto& v : drawList) {
    const auto offset = _offset + ostep * v.depth;
    const auto yPos = wpos.y() + offset;

    if( yPos > yMax || yPos + ostep < yMin ) {
      continue;
    }

    switch(v.type) {
      case tracy::TimelineDrawType::Folded: {

        break;
      }
      case tracy::TimelineDrawType::Zone: {
        auto& ev = *(const tracy::ZoneEvent*)v.ev.get();
        const auto end = worker.GetZoneEnd( ev );
        const auto zsz = std::max( ( end - ev.Start() ) * pxns, pxns * 0.5 );
        const auto zoneColor = getZoneColorData(ev, tid, v.depth, v.inheritedColor);
        const char* zoneName = worker.GetZoneName( ev );

        auto tsz = getFontSize(zoneName);
        if( viewData.shortenName == ShortenName::Always ||
          ((viewData.shortenName == ShortenName::NoSpace || viewData.shortenName == ShortenName::NoSpaceAndNormalize )
            && tsz.width() > zsz ) )
        {
          zoneName = shortenZoneName( viewData.shortenName, zoneName, tsz, zsz );
        }

        const auto pr0 = ( ev.Start() - viewData.zvStart ) * pxns;
        const auto pr1 = ( end - viewData.zvStart ) * pxns;
        const auto px0 = std::max( pr0, -10.0 );
        const auto px1 = std::max( { std::min( pr1, double( w + 10 ) ), px0 + pxns * 0.5, px0 + MinVisSize } );
        painter->fillRect(px0, offset, px1 - px0, tsz.height(), QColor(zoneColor.color));
        if (zoneColor.highlight) {
          // TODO hight light
        }
        else {
          const auto darkColor = tracy::DarkenColor(zoneColor.color);
          drawPolyLine(painter, dpos + QPointF(px0, offset + tsz.height()),
            dpos + QPointF(px0, offset),
            dpos + QPointF(px1 - 1, offset), zoneColor.accentColor, zoneColor.thickness);
          drawPolyLine(painter, dpos + QPointF(px0, offset + tsz.height()),
            dpos + QPointF(px1 - 1, offset + tsz.height()),
            dpos + QPointF(px1 - 1, offset),
            darkColor, zoneColor.thickness);
        }
        if (tsz.width() < zsz) {
          const auto x = (ev.Start() - viewData.zvStart ) * pxns + (( end - ev.Start()) * pxns - tsz.width()) / 2;
          if( x < 0 || x > w - tsz.width() ) {
            drawTextContrast(painter, wpos + QPointF(std::max(std::max(0., px0), std::min(double(w - tsz.width()), x)), offset),
              0xFFFFFFFF, zoneName);
          }
          else if(ev.Start() == ev.End()) {
            drawTextContrast(painter, wpos + QPointF(px0 + (px1 - px0 - tsz.width()) * 0.5, offset), 0xFFFFFFFF, zoneName);
          }
          else {
            drawTextContrast(painter, wpos + QPointF(x, offset), 0xFFFFFFFF, zoneName);
          }
        }
        else {
          QPointF topleft = wpos + QPoint(px0, offset - 1);
          QPointF bottomright = wpos + QPoint(px1 - px0, tsz.height() + 1);
          painter->setClipRect(topleft.x(), topleft.y(), bottomright.x(), bottomright.y());
          drawTextContrast(painter, wpos + QPointF(std::max(int64_t( 0 ), ev.Start() - viewData.zvStart) * pxns, offset), 0xFFFFFFFF, zoneName );
          painter->setClipRect(0, 0, 0, 0, Qt::NoClip);
        }
        break;
      }
      default:
        assert(false);
        break;
    }
  }
}

void TimelineView::drawThread(const TimelineContext& context, const tracy::ThreadData& thread,
      const std::vector<tracy::TimelineDraw>& draws, int& offset, int depth, QPainter* painter) {
  
  const auto& wpos = context.wpos;
  const auto ty = context.ty;
  const auto ostep = ty + 1;
  const auto yMin = context.yMin;
  const auto yMax = context.yMax;

  // TODO draw samles and switch

  const auto yPos = wpos.y() + offset;
  if(!draws.empty() && yPos <= yMax && yPos + ostep * depth >= yMin)
  {
    drawZonelist(context, draws, offset, thread.id, painter);
  }
  offset += ostep * depth;
}

void TimelineView::drawTimeline(QPainter* painter) {
  auto timespan = viewData.zvEnd - viewData.zvStart;
  const auto width = this->width();
  auto pxns = width / double(timespan);

  const auto timeBegin = worker.GetFirstTime();
  const auto timeEnd = worker.GetLastTime();
  QPoint topLeft = QPoint(0, 0);
  QPoint bottomRight = QPoint(this->width(), this->height());
  if (timeBegin > viewData.zvStart) {
    topLeft.setX((timeBegin - viewData.zvStart) * pxns);
  }
  if (timeEnd < viewData.zvEnd) {
    topLeft.setX((timeEnd - viewData.zvStart) * pxns);
  }
  painter->fillRect(QRectF(topLeft, bottomRight), QColor("#"));

  timelineController.begin();
  if (worker.AreFramesUsed()) {
    auto& frames = worker.GetFrames();
    for (auto frameData : frames) {
      if (vis(frameData)) {
        drawTimelineFrames(painter);
      }
    }
  }

  const auto yMin = 0;
  const auto yMax = this->height();

  if (viewData.drawGpuZones) {
    for (auto& v : worker.GetGpuData()) {
      // TODO Add GPUTimeline item
    }
  }
  if (viewData.drawCpuData && worker.HasContextSwitches()) {
    // TODO Add CpuTimeline item
  }
  if (viewData.drawZones) {
    const auto& threadData = worker.GetThreadData();
    if (threadData.size() != threadOrder.size()) {
      threadOrder.reserve(threadData.size());
      size_t numReinsert = threadReinsert.size();
      size_t numNew = threadData.size() - threadOrder.size() - numReinsert;
      for( size_t i = 0; i < numReinsert + numNew; i++ )
      {
        const tracy::ThreadData *td = i < numReinsert ? threadReinsert[i] : threadData[threadOrder.size()];
        auto it = std::find_if( threadOrder.begin(), threadOrder.end(), [td]( const auto t ) {
          return td->groupHint < t->groupHint;
        } );
        threadOrder.insert( it, td );
      }
      threadReinsert.clear();
    }
    for (const auto& v : threadOrder) {
      timelineController.addItem<TimelineItemThread>(v);
    }
  }

  if (viewData.drawPlots) {
    for (const auto& v : worker.GetPlots()) {
      // TODO Add plotTimeline item
    }
  }

  timelineController.end(pxns, QPoint(0, 0), true, yMin, yMax, painter);
}

void TimelineView::paintEvent(QPaintEvent* event) {
  auto painter = QPainter(this->viewport());
  drawTimeline(&painter);
  QGraphicsView::paintEvent(event);
}

void TimelineView::mouseMoveEvent(QMouseEvent* event) {
  return QWidget::mouseMoveEvent(event);
}

void TimelineView::wheelEvent(QWheelEvent* event) {
  event->accept();
}