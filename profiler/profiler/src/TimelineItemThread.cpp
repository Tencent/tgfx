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

#include "TimelineItemThread.h"
#include <src/profiler/TracyColor.hpp>
#include "TracyColor.hpp"
#include "View.h"
#include "src/profiler/TracyTimelineDraw.hpp"

constexpr float MinVisSize = 3;
constexpr float MinCtxSize = 4;

TimelineItemThread::TimelineItemThread(TimelineView& view, tracy::Worker& worker, const tracy::ThreadData* thread)
  : TimelineItem(view, worker, thread)
  , threadData(thread) {
  auto name = worker.GetThreadName(thread->id);
  if (strncmp(name, "Tracy", 6) == 0) {
    showFull = false;
  }
}

int TimelineItemThread::preprocessZoneLevel(const TimelineContext& ctx, const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& vec,
    int depth, bool visible, const uint32_t inheritedColor) {
  if (vec.is_magic()) {
    return preprocessZoneLevel<tracy::VectorAdapterDirect<tracy::ZoneEvent>>
    (ctx, *(tracy::Vector<tracy::ZoneEvent>*)(&vec), depth, visible, inheritedColor);
  }
  else {
    return preprocessZoneLevel<tracy::VectorAdapterPointer<tracy::ZoneEvent>>
    ( ctx, vec, depth, visible, inheritedColor );
  }
}

template<typename Adapter, typename V>
int TimelineItemThread::preprocessZoneLevel(const TimelineContext& ctx, const V& vec, int depth, bool visible, const uint32_t inheritedColor) {
  const auto vStart = ctx.vStart;
  const auto vEnd = ctx.vEnd;
  const auto nspx = ctx.nspx;

  // const auto MinVisNs = int64_t( round( GetScale() * MinVisSize * nspx ) );
  const auto MinVisNs = int64_t(round(MinVisSize * nspx));

  auto it = std::lower_bound( vec.begin(), vec.end(), vStart, [this] ( const auto& l, const auto& r ) { Adapter a; return worker.GetZoneEnd( a(l) ) < r; } );
  if( it == vec.end() ) return depth;

  const auto zitend = std::lower_bound( it, vec.end(), vEnd, [] ( const auto& l, const auto& r ) { Adapter a; return a(l).Start() < r; } );
  if(it == zitend) return depth;

  Adapter a;
  if(!a(*it).IsEndValid() && worker.GetZoneEnd( a(*it)) < vStart) return depth;
  if(worker.GetZoneEnd( a(*(zitend-1))) < vStart) return depth;

  int maxdepth = depth + 1;
  while( it < zitend )
  {
      auto& ev = a(*it);
      const auto end = worker.GetZoneEnd( ev );
      const auto zsz = end - ev.Start();
      if( zsz < MinVisNs )
      {
          auto nextTime = end + MinVisNs;
          auto next = it + 1;
          for(;;)
          {
              next = std::lower_bound( next, zitend, nextTime, [this] ( const auto& l, const auto& r ) { Adapter a; return worker.GetZoneEnd( a(l) ) < r; } );
              if( next == zitend ) break;
              auto prev = next - 1;
              const auto pt = worker.GetZoneEnd( a(*prev) );
              const auto nt = worker.GetZoneEnd( a(*next) );
              if( nt - pt >= MinVisNs ) break;
              nextTime = nt + MinVisNs;
          }
          if( visible ) draws.emplace_back( tracy::TimelineDraw { tracy::TimelineDrawType::Folded, uint16_t( depth ), (void**)&ev, worker.GetZoneEnd( a(*(next-1)) ), uint32_t( next - it ), inheritedColor } );
          it = next;
      }
      else
      {
          const auto hasChildren = ev.HasChildren();
          auto currentInherited = inheritedColor;
          auto childrenInherited = inheritedColor;
          if( timelineView.getViewData().inheritParentColors )
          {
              uint32_t color = 0;
              if( worker.HasZoneExtra( ev ) )
              {
                  const auto& extra = worker.GetZoneExtra( ev );
                  color = extra.color.Val();
              }
              if( color == 0 )
              {
                  auto& srcloc = worker.GetSourceLocation( ev.SrcLoc() );
                  color = srcloc.color;
              }
              if( color != 0 )
              {
                  currentInherited = color | 0xFF000000;
                  if( hasChildren ) childrenInherited = tracy::DarkenColorSlightly(color);
              }
          }
          if( hasChildren )
          {
              const auto d = preprocessZoneLevel( ctx, worker.GetZoneChildren( ev.Child() ), depth + 1, visible, childrenInherited );
              if( d > maxdepth ) maxdepth = d;
          }
          if( visible ) draws.emplace_back(tracy::TimelineDraw {tracy::TimelineDrawType::Zone, uint16_t( depth ), (void**)&ev, 0, 0, currentInherited } );
          ++it;
      }
    }
    return maxdepth;
}

void TimelineItemThread::preprocess(const TimelineContext& ctx, tracy::TaskDispatch& td,
                                    bool visible, int yPos) {
  td.Queue([this, &ctx, visible] {
    depth = preprocessZoneLevel(ctx, threadData->timeline, 0, visible, 0);
  });

  const auto& viewData = timelineView.getViewData();
  if (viewData.drawContextSwitches) {
    // TODO draw context switches
  }

  hasSamples = false;
  if (viewData.drawSamples && !threadData->samples.empty()) {
    // TODO draw samples
  }

  hasMessage = false;
  td.Queue([this, &ctx, visible, yPos] {
    // TODO process message
  });

  if (viewData.drawLocks) {
    // TODO process locks
  }
}

void TimelineItemThread::drawFinished() {
  draws.clear();
}

bool TimelineItemThread::drawContent(const TimelineContext& ctx, int& offset, QPainter* painter) {
  timelineView.drawThread(ctx, *threadData, draws, offset, depth, painter);
  return true;
}

uint32_t TimelineItemThread::headerColor() const {
  auto& crash = worker.GetCrashEvent();
  if( crash.thread == threadData->id ) return 0xFF2222FF;
  if( threadData->isFiber ) return 0xFF88FF88;
  return 0xFFFFFFFF;
}

uint32_t TimelineItemThread::headerColorInactive() const {
  auto& crash = worker.GetCrashEvent();
  if( crash.thread == threadData->id ) return 0xFF111188;
  if( threadData->isFiber ) return 0xFF448844;
  return 0xFF888888;
}

uint32_t TimelineItemThread::headlineColor() const {
    return 0x33FFFFFF;
}

const char* TimelineItemThread::headerLable() const {
  return worker.GetThreadName( threadData->id );
}

