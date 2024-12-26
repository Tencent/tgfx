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
#include <QPushButton>
#include <QWidget>
#include <QWindow>
#include "TimelineController.h"
#include "src/profiler/TracyTimelineDraw.hpp"
#include "TracyWorker.hpp"
#include "ViewData.h"

class BatchDraw {
public:
  BatchDraw(QPainter* painter = nullptr): painter(painter) {
    font = QFont("Arial", 12);
  }
  void setPainter(QPainter* painter) { this->painter = painter; }

  void addLines(QPointF p1, QPointF p2, uint32_t color);
  void addRect(QRect rect, uint32_t color);
  void addText(const char* text, QPointF pos, uint32_t color);
  void draw();
private:
  QFont font;
  QPainter* painter;
  std::unordered_map<uint32_t, QVector<QLineF>> lines;
  std::unordered_map<uint32_t, QPainterPath> paths;
};

class TimelineView: public QWidget {
public:
  struct Region
  {
    bool active = false;
    int64_t start;
    int64_t end;
  };

  struct Animation {
    bool active = false;
    int64_t start0, start1;
    int64_t end0, end1;
    double progress;
  };

  struct ZoneColorData {
    uint32_t color;
    uint32_t accentColor;
    float thickness;
    bool highlight;
  };

  struct HoverData {
    bool hover = false;
    QPointF pos;
  };

  struct MoveData {
    bool isDragging = false;
    QPointF pos;
    double hwheelDelta = 0;
  };

  enum class ViewMode
  {
    Paused,
    LastFrames,
    LastRange
};
  TimelineView(tracy::Worker& worker, ViewData& viewData, bool threadedRendering, QWidget* parent = nullptr);
  ~TimelineView();

  ViewData& getViewData() {return viewData;}

  void drawThread(const TimelineContext& context, const tracy::ThreadData& thread,
      const std::vector<tracy::TimelineDraw>& draw, int& _offset, int depth, QPainter* painter);
  void drawZonelist(const TimelineContext& ctx, const std::vector<tracy::TimelineDraw>& drawList,
    int offset, uint64_t tid, QPainter* painter);
  void drawMouseLine(QPainter* painter);
  void drawTimeline(QPainter* painter);
  void drawTimelineFrames(QPainter* painter, tracy::FrameData& fd, int& yMin);

  void zoomToRange(int64_t start, int64_t end, bool pause);

  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

  const char* getFrameText(const tracy::FrameData& fd, int i, uint64_t ftime) const;
  const char* getFrameSetName(const tracy::FrameData& fd) const;
  const char* getFrameSetName(const tracy::FrameData& fd, const tracy::Worker& worker) const;
  uint64_t getFrameNumber(const tracy::FrameData& fd, int i) const;
  uint32_t getZoneColor(const tracy::ZoneEvent& ev, uint64_t thread, int depth);
  uint32_t getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth);
  ZoneColorData getZoneColorData(const tracy::ZoneEvent& ev, uint64_t thread, int depth, uint32_t inheritedColor);
protected:
  tracy_force_inline bool& vis(const void* ptr) {
    auto it = visMap.find(ptr);
    if(it == visMap.end()) {
      it = visMap.emplace(ptr, true).first;
    }
    return it->second;
  }
private:
  tracy::unordered_flat_map<const void*, bool> visMap;
  tracy::Vector<const tracy::ThreadData*> threadOrder;
  tracy::Vector<const tracy::ThreadData*> threadReinsert;

  tracy::Worker& worker;
  ViewData& viewData;
  TimelineController timelineController;

  BatchDraw batchDraw;
  Region hightlight;
  Region hightlightZoom;
  Animation zoomAnim;
  ViewMode viewMode;
  HoverData hoverData;
  const tracy::FrameData* frameData;
  MoveData moveData;
};