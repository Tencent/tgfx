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
#include <tgfx/gpu/opengl/qt/QGLWindow.h>
#include <QQuickItem>
#include "TracyWorker.hpp"
#include "ViewData.h"

#define FRAME_VIEW_HEIGHT 50

constexpr int64_t MaxFrameTime = 50 * 1000 * 1000;

class TimelineView;
class FramesView : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(unsigned long long worker READ getWorker WRITE setWorker)
  Q_PROPERTY(ViewData* viewData READ getViewDataPtr WRITE setViewData)
  Q_PROPERTY(unsigned long long viewMode READ getViewMode WRITE setViewMode)
public:
  FramesView(QQuickItem* parent = nullptr);
  ~FramesView();
  void initView();
  void createAppHost();
  void setViewToLastFrames();

  void wheelEvent(QWheelEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void hoverMoveEvent(QHoverEvent* event) override;

  uint64_t getFrameNumber(const tracy::FrameData& frameData, uint64_t i);

  unsigned long long getWorker() const { return (unsigned long long)worker; }
  void setWorker(unsigned long long _worker) {
    worker = (tracy::Worker*)(_worker);
    frames = worker->GetFramesBase();
  }

  ViewData* getViewDataPtr() const { return viewData; }
  void setViewData(ViewData* _viewData) {
    viewData = _viewData;
    if (viewData) {
      frameTarget = 1000 * 1000 * 1000 / viewData->frameTarget;
    }
  }

  //set timelineView
  void setTimelineView(TimelineView* timelineView){m_timelineView = timelineView;}

  unsigned long long getViewMode() const {return (unsigned long long)viewMode;}
  void setViewMode(unsigned long long _viewMode) { viewMode = (ViewMode*)_viewMode; }

  //hover information utility.
  void showFrameTip(const tracy::FrameData* frames);

  //signals
  Q_SIGNAL void changeViewMode(ViewMode mode);

protected:
  void draw();
  void drawFrames(tgfx::Canvas* canvas);
  void drawBackground(tgfx::Canvas* canvas);
  QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData*) override;

private:
  tracy::Worker* worker = nullptr;
  ViewData* viewData;
  ViewMode* viewMode;
  const tracy::FrameData* frames = nullptr;
  TimelineView* m_timelineView = nullptr;

  uint64_t frameTarget;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;

  //hover tip
  uint64_t frameHover = 0;

  //Left button
  float viewOffset = 0.0f;
  size_t selectedStartFrame;
  size_t selectedEndFrame;
  bool isLeftDagging;
  QPoint lastLeftDragPos;
  size_t dragStartFrame;

  //RightButton
  bool isRightDragging = false;
  QPoint lastRightDragPos;


};