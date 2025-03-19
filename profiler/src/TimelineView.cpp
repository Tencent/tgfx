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

#include "TimelineView.h"
#include <qevent.h>
#include <QLabel>
#include <QMessageBox>
#include <QSGImageNode>
#include <QToolTip>
#include <QVBoxLayout>
#include <cinttypes>
#include "TimelineItemThread.h"
#include "TracyPrint.hpp"
#include "src/profiler/TracyColor.hpp"

constexpr float MinVisSize = 3;
constexpr float MinFrameSize = 5;

static tracy_force_inline uint32_t getColorMuted(uint32_t color, bool active) {
  if (active) {
    return 0xFF000000 | color;
  } else {
    return 0x66000000 | color;
  }
}

TimelineView::TimelineView(QQuickItem* parent)
  :QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
  createAppHost();
  initConnect();
}

TimelineView::~TimelineView() {
}


void TimelineView::initConnect() {
  connect(this, &TimelineView::showZoneToolTipSignal, this, &TimelineView::showZoneToolTip);
  connect(this, &TimelineView::showFlodedToolTipSignal, this, &TimelineView::showFlodedToolTip);
}

uint32_t TimelineView::getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth) {
  auto namehash = srcloc.namehash;
  if (namehash == 0 && srcloc.function.active) {
    const auto f = worker->GetString(srcloc.function);
    namehash = static_cast<uint32_t>(tracy::charutil::hash(f));
    if (namehash == 0) namehash++;
    srcloc.namehash = namehash;
  }
  if (namehash == 0) {
    return tracy::GetHsvColor(uint64_t(&srcloc), depth);
  } else {
    return tracy::GetHsvColor(namehash, depth);
  }
}

uint32_t TimelineView::getZoneColor(const tracy::ZoneEvent& ev, uint64_t thread, int depth) {
  const auto sl = ev.SrcLoc();
  const auto& srcloc = worker->GetSourceLocation(sl);
  if (!viewData->forceColors) {
    if (worker->HasZoneExtra(ev)) {
      const auto custom_color = worker->GetZoneExtra(ev).color.Val();
      if (custom_color != 0) return custom_color | 0xFF000000;
    }
    const auto color = srcloc.color;
    if (color != 0) return color | 0xFF000000;
  }
  switch (viewData->dynamicColors) {
    case 0:
      return 0xFFCC5555;
    case 1:
      return tracy::GetHsvColor(thread, depth);
    case 2:
      return getRawSrcLocColor(srcloc, depth);
    default:
      assert(false);
      return 0;
  }
}

TimelineView::ZoneColorData TimelineView::getZoneColorData(const tracy::ZoneEvent& ev,
                                                           uint64_t thread, int depth,
                                                           uint32_t inheritedColor) {
  ZoneColorData ret;
  const auto color = inheritedColor ? inheritedColor : getZoneColor(ev, thread, depth);
  ret.color = color;
  ret.accentColor = tracy::HighlightColor(color);
  ret.thickness = 1.f;
  ret.highlight = false;
  return ret;
}

uint64_t TimelineView::getFrameNumber(const tracy::FrameData& fd, int i) const {
  auto idx = static_cast<uint64_t>(i);
  if (fd.name == 0) {
    const auto offset = worker->GetFrameOffset();
    if (offset == 0) {
      return idx;
    } else {
      return idx + offset - 1;
    }
  } else {
    return idx + 1;
  }
}

const char* TimelineView::getFrameSetName(const tracy::FrameData& fd) const {
  return getFrameSetName(fd, worker);
}

const char* TimelineView::getFrameSetName(const tracy::FrameData& fd,
                                          const tracy::Worker* worker) const {
  enum { Pool = 4 };
  static char bufpool[Pool][64];
  static size_t bufsel = 0;

  if (fd.name == 0) {
    return "Frames";
  } else if (fd.name >> 63 != 0) {
    char* buf = bufpool[bufsel];
    bufsel = (bufsel + 1) % Pool;
    snprintf(buf, bufsel, "[%" PRIu32 "] Vsync", uint32_t(fd.name));
    return buf;
  } else {
    return worker->GetString(fd.name);
  }
}

const char* TimelineView::getFrameText(const tracy::FrameData& fd, int i, uint64_t ftime) const {
  auto frameTime = static_cast<int64_t>(ftime);
  const auto fnum = static_cast<double>(getFrameNumber(fd, i));
  static char buf[1024];
  if (fd.name == 0) {
    if (i == 0) {
      snprintf(buf, 1024, "Tracy init (%s)", tracy::TimeToString(frameTime));
    } else if (i != 1 || !worker->IsOnDemand()) {
      snprintf(buf, 1024, "Frame %s (%s)", tracy::RealToString(fnum),
               tracy::TimeToString(frameTime));
    } else {
      snprintf(buf, 1024, "Missed frames (%s)", tracy::TimeToString(frameTime));
    }
  } else {
    snprintf(buf, 1024, "%s %s (%s)", getFrameSetName(fd), tracy::RealToString(fnum),
             tracy::TimeToString(frameTime));
  }
  return buf;
}

void TimelineView::drawMouseLine(tgfx::Canvas* canvas) {
  if (!mouseLine.isVisible) {
    return;
  }
  const auto lineColor = 0x33FFFFFF;

  const auto linePos = 0.0f;
  const auto lineH = static_cast<float>(height());

  auto p1 = tgfx::Point{mouseData.x + 0.5f, linePos + 0.5f};
  auto p2 = tgfx::Point{mouseData.x + 0.5f, linePos + lineH + 0.5f};

  drawLine(canvas, p1, p2, lineColor);
}

void TimelineView::drawTimelineFrames(tgfx::Canvas* canvas, tracy::FrameData& fd, int& yMin) {
  const std::pair<size_t, size_t> zrange =
      worker->GetFrameRange(fd, viewData->zvStart, viewData->zvEnd);

  if (zrange.first < 0) {
    return;
  }
  if (worker->GetFrameBegin(fd, zrange.first) > viewData->zvEnd ||
      worker->GetFrameEnd(fd, zrange.second) < viewData->zvStart) {
    return;
  }
  const auto wpos = tgfx::Point{0.f, 0.f};
  const auto dpos = wpos + tgfx::Point{0.5f, 0.5f};
  const auto w = float(width());
  const auto wh = float(height());
  const auto ty = 15;
  const auto ty05 = round(ty * 0.5f);

  yMin += ty;

  auto timespan = viewData->zvEnd - viewData->zvStart;
  auto pxns = w / float(timespan);

  const auto nspx = 1.0 / pxns;

  int64_t prev = -1;
  int64_t prevEnd = -1;
  int64_t endPos = -1;
  const auto activeFrameSet = frames == &fd;
  const int64_t frameTarget = (activeFrameSet && viewData->drawFrameTargets)
                                  ? 1000000000ll / viewData->frameTarget
                                  : std::numeric_limits<int64_t>::max();

  const auto activeColor = getColorMuted(0xFFFFFF, activeFrameSet);
  const auto redColor = getColorMuted(0x4444FF, activeFrameSet);

  auto i = zrange.first;
  while (i < zrange.second) {
    const auto ftime = worker->GetFrameTime(fd, i);
    const auto fbegin = worker->GetFrameBegin(fd, i);
    const auto fend = worker->GetFrameEnd(fd, i);
    const auto fsz = pxns * ftime;

    if (fsz < MinFrameSize) {
      if (!fd.continuous && prev != -1) {
        if ((fbegin - prevEnd) * pxns >= MinFrameSize) {
          // drawImage(wpos + ImVec2(0, ty05), (prev - viewData->zvStart) * pxns, (prevEnd - viewData->zvStart) * pxns, ty025, inactiveColor);
          prev = -1;
        } else {
          prevEnd = std::max<int64_t>(fend, fbegin + int64_t(MinFrameSize * nspx));
        }
      }
      if (prev == -1) {
        prev = fbegin;
        prevEnd = std::max<int64_t>(fend, fbegin + int64_t(MinFrameSize * nspx));
      }

      const auto begin = fd.frames.begin() + i;
      const auto end = fd.frames.begin() + zrange.second;
      auto it =
          std::lower_bound(begin, end, fbegin + int64_t(MinVisSize * nspx),
                           [this, &fd](const auto& l, const auto& r) {
                             return worker->GetFrameEnd(fd, size_t(fd.frames.begin() - &l)) < r;
                           });
      if (it == begin) ++it;
      i += size_t(std::distance(begin, it));
      continue;
    }

    if (prev != -1) {
      if (fd.continuous) {
        // drawImage(draw, wpos + ImVec2(0, ty05), (prev - viewData->zvStart) * pxns, (fbegin - viewData->zvStart) * pxns, ty025, inactiveColor);
      } else {
        // drawImage(draw, wpos + ImVec2(0, ty05), (prev - viewData->zvStart) * pxns, (prevEnd - viewData->zvStart) * pxns, ty025, inactiveColor);
      }
      prev = -1;
    }

    if (activeFrameSet) {
      if (fend - fbegin > frameTarget) {
        tgfx::Point p1 = wpos + tgfx::Point{(fbegin + frameTarget - viewData->zvStart) * pxns, 0.f};
        tgfx::Point p2 = wpos + tgfx::Point{(fend - viewData->zvStart) * pxns, wh};
        drawRect(canvas, p1, p2, 0x224444FF);
      }
      if (fbegin >= viewData->zvStart && endPos != fbegin) {
        tgfx::Point p1 = dpos + tgfx::Point{(fbegin - viewData->zvStart) * pxns, 0.f};
        tgfx::Point p2 = dpos + tgfx::Point{(fbegin - viewData->zvStart) * pxns, wh};
        drawLine(canvas, p1, p2, 0x22FFFFFF);
      }
      if (fend <= viewData->zvEnd) {
        tgfx::Point p1 = dpos + tgfx::Point{(fend - viewData->zvStart) * pxns, 0.f};
        tgfx::Point p2 = dpos + tgfx::Point{(fend - viewData->zvStart) * pxns, wh};
        drawLine(canvas, p1, p2, 0x22FFFFFF);
      }
      endPos = fend;
    }

    auto buf = getFrameText(fd, int(i), uint64_t(ftime));
    auto tx = getTextSize(appHost.get(), buf).width();
    uint32_t color = (fd.name == 0 && i == 0) ? redColor : activeColor;

    if (fsz - 7 <= tx) {
      static char tmp[256];
      snprintf(tmp, 256, "%s (%s)", tracy::RealToString(i), tracy::TimeToString(ftime));
      buf = tmp;
      tx = getTextSize(appHost.get(), buf).width();
    }

    if (fsz - 7 <= tx) {
      buf = tracy::TimeToString(ftime);
      tx = getTextSize(appHost.get(), buf).width();
    }

    if (fbegin >= viewData->zvStart) {
      tgfx::Point p1 = dpos + tgfx::Point{(fbegin - viewData->zvStart) * pxns + 2.f, 1.f};
      tgfx::Point p2 = dpos + tgfx::Point{(fbegin - viewData->zvStart) * pxns + 2.f, ty - 1.f};
      drawLine(canvas, p1, p2, color);
    }
    if (fend <= viewData->zvEnd) {
      auto p1 = dpos + tgfx::Point{(fend - viewData->zvStart) * pxns - 2.f, 1.f};
      auto p2 = dpos + tgfx::Point{(fend - viewData->zvStart) * pxns - 2.f, ty - 1.f};
      drawLine(canvas, p1, p2, color);
    }

    if (fsz - 7 > tx) {
      const auto f0 = (fbegin - viewData->zvStart) * pxns + 2;
      const auto f1 = (fend - viewData->zvStart) * pxns - 2;
      const auto x0 = f0 + 1;
      const auto x1 = f1 - 1;
      const auto te = x1 - tx;

      auto tpos = (x0 + te) / 2;
      if (tpos < 0) {
        tpos = std::min(std::min(0.f, te - tpos), te);
      } else if (tpos > w - tx) {
        tpos = std::max(float(w - tx), x0);
      }
      tpos = round(tpos);

      tgfx::Point p1 = dpos + tgfx::Point{std::max(-10.0f, f0), ty05};
      tgfx::Point p2 = dpos + tgfx::Point{tpos, ty05};
      drawLine(canvas, p1, p2, color);
      p1 = dpos + tgfx::Point{std::max(-10.0f, tpos + tx + 1), ty05};
      p2 = dpos + tgfx::Point{std::min(w + 20.0f, f1), ty05};
      drawLine(canvas, p1, p2, color);
      auto textBounds = getTextSize(appHost.get(), buf);
      auto height = ty05 - textBounds.height() / 2 - textBounds.top;
      auto textPos = wpos + tgfx::Point{tpos, height};
      drawText(canvas, appHost.get(), buf, textPos.x, textPos.y, color);
    } else {
      auto p1 = dpos + tgfx::Point{std::max(-10.0f, (fbegin - viewData->zvStart) * pxns + 2), ty05};
      auto p2 =
          dpos + tgfx::Point{std::min(w + 20.0f, (fend - viewData->zvStart) * pxns - 2), ty05};
      drawLine(canvas, p1, p2, color);
    }
    i++;
  }
}

void TimelineView::drawZonelist(const TimelineContext& ctx,
                                const std::vector<tracy::TimelineDraw>& drawList, int _offset,
                                uint64_t tid, tgfx::Canvas* canvas) {
  const auto w = ctx.w;
  const auto& wpos = ctx.wpos;
  const auto dpos = tgfx::Point{0.5f, 0.5f};
  const auto ty = ctx.ty;
  const auto ostep = ty + 1;
  const auto pxns = ctx.pxns;
  const auto vStart = ctx.vStart;
  const auto zoneHeight = ty;

  for (auto& v : drawList) {
    const auto offset = _offset + ostep * v.depth;
    const auto yPos = wpos.y + offset;

    if (yPos > ctx.yMax || yPos + ostep < ctx.yMin) {
      continue;
    }

    switch (v.type) {
      case tracy::TimelineDrawType::Folded: {
        auto& ev = *(const tracy::ZoneEvent*)v.ev.get();
        const auto color = v.inheritedColor
                               ? v.inheritedColor
                               : (viewData->dynamicColors == 2
                                      ? 0xFF666666
                                      : getThreadColor(tid, v.depth, viewData->dynamicColors));
        const auto rend = v.rend.Val();
        const auto px0 = (ev.Start() - vStart) * pxns;
        const auto px1 = (rend - vStart) * pxns;
        const auto tmp = tracy::RealToString(v.num);
        const auto tsz = getTextSize(appHost.get(), tmp);
        auto p1 = wpos + tgfx::Point{std::max(px0, -10.0f) + zoneMargin, offset + zoneMargin};
        auto p2 = tgfx::Point{std::min(std::max(px1 - px0, MinVisSize), w + 10.f) - zoneMargin,
                              zoneHeight - zoneMargin};

        drawRect(canvas, p1, p2, color);
        //folded zone hover and clicked
        if (mouseData.x >= p1.x && mouseData.x <= p1.x + p2.x && mouseData.y >= p1.y &&
            mouseData.y <= p1.y + p2.y) {
          hoverData.zoneHover = &ev;
          if (v.num > 1) {
            emit showFlodedToolTipSignal(v.num, rend - ev.Start());
          } else {
            emit showZoneToolTipSignal(ev);
          }
          const auto hoverBorderColor = 0xFFFFFFFF;
          const auto thickness = tgfx::Point{1.f, 1.f};
          tgfx::Point hoverP1 = p1 + thickness;
          tgfx::Point hoverP2 = p2 - thickness;
          drawRect(canvas, hoverP1, hoverP2, hoverBorderColor, 1.f);
        }
        if (tsz.width() < px1 - px0) {
          const auto x = px0 + (px1 - px0 - tsz.width()) / 2;
          const auto y = (zoneHeight - tsz.height()) / 2 + offset;
          drawTextContrast(canvas, appHost.get(), wpos.x + x, wpos.y + y, 0xFF4488DD, tmp);
        }
        break;
      }
      case tracy::TimelineDrawType::Zone: {
        auto& ev = *(const tracy::ZoneEvent*)v.ev.get();
        const auto end = worker->GetZoneEnd(ev);
        const auto zsz = static_cast<float>(std::max((end - ev.Start()) * pxns, pxns * 0.5f));
        const auto zoneColor = getZoneColorData(ev, tid, v.depth, v.inheritedColor);
        const char* zoneName = worker->GetZoneName(ev);
        const auto px0 = (ev.Start() - vStart) * pxns;
        const auto px1 = (end - vStart) * pxns;
        auto p1 = tgfx::Point{px0 + wpos.x + zoneMargin, offset + wpos.y + zoneMargin};
        auto p2 = tgfx::Point{zsz - zoneMargin, zoneHeight - zoneMargin};
        drawRect(canvas, p1, p2, zoneColor.color);

        float thickness = 1.f;
        auto highLightColor = 0xFFFFFFFF;
        //zone hover and clicked
        bool hightLight = false;
        auto hoverP1 = p1;
        auto hoverP2 = p2;
        if (mouseData.x >= p1.x && mouseData.x <= p1.x + p2.x && mouseData.y >= p1.y &&
            mouseData.y <= p1.y + p2.y) {
          hoverData.zoneHover = &ev;
          emit showZoneToolTipSignal(ev);
          hightLight = true;
        }

        if (hoverData.isPressed && hoverData.selectedZone == &ev) {
          highLightColor = 0xFF00FF00;
          thickness = 2.f;
          const auto thicknessPoint = tgfx::Point{1.f, 1.f};
          hoverP1 = p1 + thicknessPoint;
          hoverP2 = p2 - thicknessPoint;
          hightLight = true;
        }

        if (hightLight) {
          drawRect(canvas, hoverP1, hoverP2, highLightColor, thickness);
        } else {
          tgfx::Point lp1 = dpos + tgfx::Point{p1.x, p1.y + p2.y};
          tgfx::Point lp2 = dpos + tgfx::Point{p1.x, p1.y};
          tgfx::Point lp3 = dpos + tgfx::Point{p1.x + p2.x - 1, p1.y};
          drawLine(canvas, lp1, lp2, lp3, zoneColor.accentColor);

          const auto darkColor = tracy::DarkenColor(zoneColor.color);
          lp1 = dpos + tgfx::Point{p1.x, p1.y + p2.y};
          lp2 = dpos + tgfx::Point{p1.x + p2.x - 1, p1.y + p2.y};
          lp3 = dpos + tgfx::Point{p1.x + p2.x - 1, p1.y};
          drawLine(canvas, lp1, lp2, lp3, darkColor);
        }

        auto tsz = getTextSize(appHost.get(), zoneName);
        zoneName = shortenZoneName(appHost.get(), ShortenName::Always, zoneName, tsz, zsz);
        tsz = getTextSize(appHost.get(), zoneName);
        auto tszHeight = offset + (MaxHeight - tsz.height()) / 2;
        if (tsz.width() < p2.x) {
          const auto x =
              (ev.Start() - viewData->zvStart) * pxns + (zsz - tsz.width() + zoneMargin) / 2;
          if (x < 0 || x > w - tsz.width()) {
            tgfx::Point bottomright = wpos + tgfx::Point{p2.x, tsz.height() * 2};
            auto rect = tgfx::Rect::MakeXYWH(p1.x, p1.y, bottomright.x, bottomright.y);
            canvas->save();
            canvas->clipRect(rect);
            drawTextContrast(canvas, appHost.get(),
                             wpos + tgfx::Point{std::max(std::max(0.f, px0),
                                                         std::min(float(w - tsz.width()), x)),
                                                tszHeight},
                             0xFFFFFFFF, zoneName);
            canvas->restore();
          } else if (ev.Start() == ev.End()) {
            drawTextContrast(canvas, appHost.get(),
                             wpos + tgfx::Point{px0 + (px1 - px0 - tsz.width()) * 0.5f, tszHeight},
                             0xFFFFFFFF, zoneName);
          } else {
            drawTextContrast(canvas, appHost.get(), wpos + tgfx::Point{x, tszHeight}, 0xFFFFFFFF,
                             zoneName);
          }
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
                              const std::vector<tracy::TimelineDraw>& draws, int& offset, int depth,
                              tgfx::Canvas* canvas) {

  const auto& wpos = context.wpos;
  const auto ty = context.ty;
  const auto ostep = ty + 1;
  const auto yMin = context.yMin;
  const auto yMax = context.yMax;

  // TODO draw samles and switch

  const auto yPos = wpos.y + offset;
  if (!draws.empty() && yPos <= yMax && yPos + ostep * depth >= yMin) {
    drawZonelist(context, draws, offset, thread.id, canvas);
  }
  offset += int(ostep * depth);
}

void TimelineView::drawTimeline(tgfx::Canvas* canvas) {
  auto timespan = viewData->zvEnd - viewData->zvStart;
  const auto width = static_cast<float>(this->width());
  auto pxns = width / float(timespan);

  auto yMin = 0;
  const auto yMax = this->height();
  const auto timeBegin = worker->GetFirstTime();
  const auto timeEnd = worker->GetLastTime();
  tgfx::Point topLeft = tgfx::Point{0, 0};
  tgfx::Point bottomRight = tgfx::Point{width, (float)height()};
  if (timeBegin > viewData->zvStart) {
    topLeft.x = (timeBegin - viewData->zvStart) * pxns;
  }
  if (timeEnd < viewData->zvEnd) {
    topLeft.x = (timeEnd - viewData->zvStart) * pxns;
  }
  drawRect(canvas, topLeft.x, topLeft.y, bottomRight.x, bottomRight.y, 0x44000000);

  timelineController->begin();
  if (worker->AreFramesUsed()) {
    auto& frames = worker->GetFrames();
    for (auto frameData : frames) {
      if (vis(frameData)) {
        drawTimelineFrames(canvas, *frameData, yMin);
      }
    }
  }

  if (viewData->drawZones) {
    const auto& threadData = worker->GetThreadData();
    if (threadData.size() != threadOrder.size()) {
      threadOrder.reserve(threadData.size());
      size_t numReinsert = threadReinsert.size();
      size_t numNew = threadData.size() - threadOrder.size() - numReinsert;
      for (size_t i = 0; i < numReinsert + numNew; i++) {
        const tracy::ThreadData* td =
            i < numReinsert ? threadReinsert[i] : threadData[threadOrder.size()];
        auto it = std::find_if(threadOrder.begin(), threadOrder.end(),
                               [td](const auto t) { return td->groupHint < t->groupHint; });
        threadOrder.insert(it, td);
      }
      threadReinsert.clear();
    }
    for (const auto& v : threadOrder) {
      timelineController->addItem<TimelineItemThread>(v);
    }
  }

  timelineController->end(pxns, tgfx::Point{0.f, static_cast<float>(yMin)},
                          static_cast<float>(yMin), static_cast<float>(yMax), canvas,
                          appHost.get());

  if (mouseLine.isVisible && hoverData.hover) {
    drawMouseLine(canvas);
  }
}

void TimelineView::zoomToRangeFrame(int startFrame, int endFrame, bool pause) {
  auto start = worker->GetFrameTime(*frames, size_t(startFrame));
  auto end = worker->GetFrameTime(*frames, size_t(endFrame));
  zoomToRange(start, end, pause);
}

void TimelineView::zoomToRange(int64_t start, int64_t end, bool pause) {
  if (start == end) {
    end = start + 1;
  }

  if (pause) {
    *viewMode = ViewMode::Paused;
  }
  hightlightZoom.active = false;
  zoomAnim.active = true;
  if (*viewMode == ViewMode::LastRange) {
    const auto rangeCurr = viewData->zvEnd - viewData->zvStart;
    const auto rangeDest = end - start;
    zoomAnim.start0 = viewData->zvStart;
    zoomAnim.start1 = viewData->zvStart - (rangeDest - rangeCurr);
    zoomAnim.end0 = viewData->zvEnd;
    zoomAnim.end1 = viewData->zvEnd;
  } else {
    zoomAnim.start0 = viewData->zvStart;
    zoomAnim.start1 = start;
    zoomAnim.end0 = viewData->zvEnd;
    zoomAnim.end1 = end;
  }
  zoomAnim.progress = 0;
}

tgfx::Color TimelineView::getColor(uint32_t color) {
  uint8_t r = (color)&0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = (color >> 16) & 0xFF;
  uint8_t a = (color >> 24) & 0xFF;
  return tgfx::Color::FromRGBA(r, g, b, a);
}

void TimelineView::createAppHost() {
  appHost = std::make_unique<AppHost>();
#ifdef __APPLE__
  auto defaultTypeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
  auto emojiTypeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
#else
  auto defaultTypeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
#endif
  appHost->addTypeface("default", defaultTypeface);
}

void TimelineView::draw() {
  auto device = tgfxWindow->getDevice();
  if (device == nullptr) {
    return;
  }
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  auto surface = tgfxWindow->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();
  drawRect(canvas, 0, 0, 3008, 1578, 0xFF2E2E2E);
  drawTimeline(canvas);
  canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}

QSGNode* TimelineView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  auto node = static_cast<QSGImageNode*>(oldNode);
  if (!tgfxWindow) {
    tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
  }
  auto pixelRatio = window()->devicePixelRatio();
  auto screenWidth = static_cast<int>(ceil(width() * pixelRatio));
  auto screenHeight = static_cast<int>(ceil(height() * pixelRatio));

  auto sizeChanged =
      appHost->updateScreen(screenWidth, screenHeight, static_cast<float>(pixelRatio));
  if (sizeChanged) {
    tgfxWindow->invalidSize();
  }
  draw();
  auto texture = tgfxWindow->getQSGTexture();
  if (texture) {
    if (node == nullptr) {
      node = window()->createImageNode();
    }
    node->setTexture(texture);
    node->markDirty(QSGNode::DirtyMaterial);
    node->setRect(boundingRect());
  }
  return node;
}

void TimelineView::mouseMoveEvent(QMouseEvent* event) {
  mouseLine.isVisible = true;
  mouseData.x = event->pos().x();
  mouseData.y = event->pos().y();

  const auto timespan = viewData->zvEnd - viewData->zvStart;
  const auto w = float(width());
  const auto nspx = float(timespan) / w;

  if (hightlight.active) {
    hightlight.end = viewData->zvStart + int64_t(event->position().x() * nspx);
    update();
  } else if (moveData.isDragging) {
    *viewMode = ViewMode::Paused;
    Q_EMIT changeViewMode(*viewMode);
    zoomAnim.active = false;
    const auto w = width();
    const auto nspx = double(timespan) / w;
    auto delta = event->position().toPoint() - moveData.pos;
    moveData.pos = event->position().toPoint();
    const auto dpx = int64_t((delta.x() * nspx) + (moveData.hwheelDelta * nspx));
    if (dpx != 0) {
      viewData->zvStart -= dpx;
      viewData->zvEnd -= dpx;
      if (viewData->zvStart < -1000ll * 1000 * 1000 * 60 * 60 * 24 * 5) {
        const auto range = viewData->zvEnd - viewData->zvStart;
        viewData->zvStart = -1000ll * 1000 * 1000 * 60 * 60 * 24 * 5;
        viewData->zvEnd = viewData->zvStart + range;
      } else if (viewData->zvEnd > 1000ll * 1000 * 1000 * 60 * 60 * 24 * 5) {
        const auto range = viewData->zvEnd - viewData->zvStart;
        viewData->zvEnd = 1000ll * 1000 * 1000 * 60 * 60 * 24 * 5;
        viewData->zvStart = viewData->zvEnd - range;
      }
    }
  } else {
    hoverData.hover = false;
    hoverData.zoneHover = nullptr;
    hoverData.hover = true;
  }
  update();
  event->accept();
}

void TimelineView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const auto timespan = viewData->zvEnd - viewData->zvStart;
    const auto w = width();
    const auto nspx = double(timespan) / w;

    if (hoverData.zoneHover) {
      hoverData.isPressed = true;
      hoverData.pressPos = event->pos();
      hoverData.selectedZone = hoverData.zoneHover;

      if (zoneInfo.window) {
        zoneInfo.window->hide();
        zoneInfo.window.reset();
      }
      showZoneInfo(*hoverData.selectedZone);
    } else {
      hightlight.active = true;
      hightlight.start = hightlight.end = viewData->zvStart + int64_t(event->position().x() * nspx);

      hoverData.isPressed = false;
      hoverData.selectedZone = nullptr;

      if (zoneInfo.window) {
        zoneInfo.window->hide();
        zoneInfo.window.reset();
      }
      zoneInfo.active = false;
    }
    update();
  } else if (event->button() == Qt::RightButton) {
    moveData.isDragging = true;
    moveData.pos = event->position().toPoint();
    moveData.hwheelDelta = 0;
    *viewMode = ViewMode::Paused;
    zoomAnim.active = false;
  }
  event->accept();
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton) {
    moveData.isDragging = false;
  }

  if (event->button() == Qt::LeftButton) {
    hightlight.active = false;
    update();
  }
  event->accept();
}

void TimelineView::hoverMoveEvent(QHoverEvent* event) {
  mouseLine.isVisible = true;
  mouseData.x = static_cast<float>(event->position().x());
  mouseData.y = static_cast<float>(event->position().y());

  hoverData.hover = true;
  hoverData.zoneHover = nullptr;

  update();
  QQuickItem::hoverMoveEvent(event);
}

//ZoneInfo and ToolTips
void TimelineView::showZoneInfo(const tracy::ZoneEvent& ev) {
  zoneInfo.window = std::make_unique<QDialog>();
  zoneInfo.window->setWindowTitle("zone information");
  connect(zoneInfo.window.get(), &QDialog::finished, this, [this]() {
    hoverData.selectedZone = nullptr;
    hoverData.isPressed = false;
    zoneInfo.active = false;
    zoneInfo.window.reset();
    update();
  });

  auto layout = new QVBoxLayout(zoneInfo.window.get());
  QString infoText;
  infoText = QString("Name:%1\n").arg(worker->GetZoneName(ev));
  const auto end = worker->GetZoneEnd(ev);

  infoText += QString("Start:%1\n").arg(QString::fromStdString(tracy::TimeToString(ev.Start())));
  infoText +=
      QString("Duration:%1\n").arg(QString::fromStdString(tracy::TimeToString(end - ev.Start())));
  infoText += QString("End:%1\n").arg(QString::fromStdString((tracy::TimeToString(end))));

  if (worker->HasZoneExtra(ev)) {
    const auto& extra = worker->GetZoneExtra(ev);
    if (extra.text.Active()) {
      infoText += QString("Text:%1").arg(worker->GetString(extra.text));
    }
  }

  auto infoLabel = new QLabel(infoText);
  infoLabel->setTextFormat(Qt::PlainText);
  infoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  layout->addWidget(infoLabel);

  zoneInfo.active = true;
  zoneInfo.window->setMinimumWidth(300);
  zoneInfo.window->show();
}

void TimelineView::showZoneToolTip(const tracy::ZoneEvent& ev) {
  //threadid
  const auto end = worker->GetZoneEnd(ev);
  const auto Ztime = end - ev.Start();

  auto ZoneMsg = QString("%1\nTime:%2\nExcution Time:%3")
                     .arg(worker->GetZoneName(ev))
                     .arg(QString::fromStdString(tracy::TimeToString(Ztime)))
                     .arg(tracy::TimeToString(end - ev.Start()));
  if (worker->HasZoneExtra(ev)) {
    const auto& extra = worker->GetZoneExtra(ev);
    if (extra.text.Active()) {
      ZoneMsg += QString("\nText:%1").arg(worker->GetString(extra.text));
    }
  }
  QToolTip::showText(QCursor::pos(), ZoneMsg);
}

void TimelineView::showFlodedToolTip(uint32_t num, int64_t time) {
  QString tooltip = QString("Zone too small to display: %1\n").arg(num);
  tooltip += QString("Excution time: %1")
                 .arg(QString::fromStdString(tracy::TimeToString(time)));
  QToolTip::showText(QCursor::pos(), tooltip);
}

void TimelineView::hoverLeaveEvent(QHoverEvent* event) {
  QToolTip::hideText();
  mouseLine.isVisible = false;
  update();
  QQuickItem::hoverLeaveEvent(event);
}

void TimelineView::wheelEvent(QWheelEvent* event) {
  if (*viewMode == ViewMode::LastFrames) {
    *viewMode = ViewMode::LastRange;
  }
  const auto mouse = mapFromGlobal(QCursor().pos());
  const auto p = double(mouse.x()) / width();
  int64_t t0, t1;
  if (zoomAnim.active) {
    t0 = zoomAnim.start1;
    t1 = zoomAnim.end1;
  } else {
    t0 = viewData->zvStart;
    t1 = viewData->zvEnd;
  }
  const auto zoomSpan = t1 - t0;
  const auto p1 = zoomSpan * p;
  const auto p2 = zoomSpan - p1;

  double mod = 0.05;
  auto wheel = event->angleDelta().y();
  if (wheel > 0) {
    t0 += int64_t(p1 * mod);
    t1 -= int64_t(p2 * mod);
  } else if (wheel < 0 && zoomSpan < 1000ll * 1000 * 1000 * 60 * 60) {
    t0 -= std::max(int64_t(1), int64_t(p1 * mod));
    t1 += std::max(int64_t(1), int64_t(p2 * mod));
  }

  viewData->zvStart = int64_t(zoomAnim.start0 + zoomAnim.start1 - zoomAnim.start0);
  viewData->zvEnd = int64_t(zoomAnim.end0 + zoomAnim.end1 - zoomAnim.end0);
  zoomToRange(t0, t1, !worker->IsConnected() || *viewMode == ViewMode::Paused);
  update();
  event->accept();
}
