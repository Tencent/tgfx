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

#include <QGraphicsLineItem>
#include "FramesView.h"
#include "TimelineController.h"
#include "TracyWorker.hpp"
#include "ViewData.h"
#include "src/profiler/TracyTimelineDraw.hpp"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"

class TimelineView : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(unsigned long long worker READ getWorker WRITE setWorker)
  Q_PROPERTY(ViewData* viewData READ getViewDataPtr WRITE setViewData)
  Q_PROPERTY(unsigned long long viewMode READ getViewMode WRITE setViewMode)
 public:
  struct Region {
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

  //hovered struct.
  struct HoverData {
    bool hover = false;
    const tracy::ZoneEvent* zoneHover = nullptr;
    const tracy::ZoneEvent* selectedZone = nullptr;
    uint32_t color = 0;
    QPointF pressPos;
    float thickness = 1.0f;
    bool isPressed = false;
  };

  //clicked zone info.
  struct ZoneInfoWindow {
    bool active = false;
    std::unique_ptr<QDialog> window;
  };

  struct MoveData {
    bool isDragging = false;
    QPointF pos;
    double hwheelDelta = 0;
  };

  // mouseLine
  struct MouseLine {
    bool isVisible = false;
  };

  TimelineView(QQuickItem* parent = nullptr);
  ~TimelineView();
  void initConnect();

  void createAppHost();

  void draw();
  void drawThread(const TimelineContext& context, const tracy::ThreadData& thread,
                  const std::vector<tracy::TimelineDraw>& draw, int& _offset, int depth,
                  tgfx::Canvas* canvas);
  void drawZonelist(const TimelineContext& ctx, const std::vector<tracy::TimelineDraw>& drawList,
                    int offset, uint64_t tid, tgfx::Canvas* canvas);
  void drawMouseLine(tgfx::Canvas* canvas);
  void drawTimeline(tgfx::Canvas* canvas);
  void drawTimelineFrames(tgfx::Canvas* canvas, tracy::FrameData& fd, int& yMin);

  void zoomToRange(int64_t start, int64_t end, bool pause);

  void wheelEvent(QWheelEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void hoverMoveEvent(QHoverEvent* event) override;
  void hoverLeaveEvent(QHoverEvent* event) override;

  //hovered functions.
  void showZoneInfo(const tracy::ZoneEvent& ev);
  void showZoneToolTip(const tracy::ZoneEvent& ev);
  void showFlodedToolTip(uint32_t num, int64_t time);

  const char* getFrameText(const tracy::FrameData& fd, int i, uint64_t ftime) const;
  const char* getFrameSetName(const tracy::FrameData& fd) const;
  const char* getFrameSetName(const tracy::FrameData& fd, const tracy::Worker* worker) const;
  uint64_t getFrameNumber(const tracy::FrameData& fd, int i) const;
  uint32_t getZoneColor(const tracy::ZoneEvent& ev, uint64_t thread, int depth);
  uint32_t getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth);
  ZoneColorData getZoneColorData(const tracy::ZoneEvent& ev, uint64_t thread, int depth,
                                 uint32_t inheritedColor);

  static tgfx::Color getColor(uint32_t color);

  ViewData* getViewData() {
    return viewData;
  }

  unsigned long long getWorker() const {
    return (unsigned long long)worker;
  }
  void setWorker(unsigned long long _worker) {
    worker = (tracy::Worker*)(_worker);
    frameData = worker->GetFramesBase();
    timelineController = new TimelineController(*this, *worker, true);
  }

  Q_SIGNAL void changeViewMode(ViewMode mode);
  Q_SIGNAL void showZoneToolTipSignal(const tracy::ZoneEvent& ev);
  Q_SIGNAL void showFlodedToolTipSignal(uint32_t num, int64_t time);
  ViewData* getViewDataPtr() const {
    return viewData;
  }
  void setViewData(ViewData* _viewData) {
    viewData = _viewData;
  }

  unsigned long long getViewMode() const {
    return (unsigned long long)viewMode;
  }
  void setViewMode(unsigned long long _viewMode) {
    viewMode = (ViewMode*)_viewMode;
  }

  void setFramesView(FramesView* framesView) {
    m_framesView = framesView;
  }

 protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
  tracy_force_inline bool& vis(const void* ptr) {
    auto it = visMap.find(ptr);
    if (it == visMap.end()) {
      it = visMap.emplace(ptr, true).first;
    }
    return it->second;
  }

 private:
  tracy::unordered_flat_map<const void*, bool> visMap;
  tracy::Vector<const tracy::ThreadData*> threadOrder;
  tracy::Vector<const tracy::ThreadData*> threadReinsert;

  FramesView* m_framesView = nullptr;
  const tracy::FrameData* frames = nullptr;
  tracy::Worker* worker;
  ViewData* viewData;
  ViewMode* viewMode;
  TimelineController* timelineController;

  Region hightlight;
  Region hightlightZoom;
  Animation zoomAnim;
  HoverData hoverData;
  const tracy::FrameData* frameData;
  MoveData moveData;
  MouseLine mouseLine;
  tgfx::Point mouseData;

  tgfx::Font font;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
  bool dirty = false;

  //hover members
  ZoneInfoWindow zoneInfo;
  bool highlightZoom = false;
};