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
#include "AppHost.h"
#include "ViewData.h"
#include "Worker.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"

namespace inspector {
#define FRAME_VIEW_HEIGHT 50

constexpr int64_t MaxFrameTime = 50 * 1000 * 1000;

class TimelineView;
class FramesDrawer : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(Worker* worker READ getWorker WRITE setWorker)
  Q_PROPERTY(ViewData* viewData READ getViewData WRITE setViewData)
 public:
  FramesDrawer(QQuickItem* parent = nullptr);
  ~FramesDrawer() = default;

  void wheelEvent(QWheelEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  // void hoverMoveEvent(QHoverEvent* event) override;

  Worker* getWorker() const {
    return worker;
  }
  void setWorker(Worker* worker) {
    this->worker = worker;
    frames = worker->getFrameData();
  }

  ViewData* getViewData() const {
    return viewData;
  }
  void setViewData(ViewData* viewData) {
    this->viewData = viewData;
  }

  Q_SIGNAL void selectFrame();

 protected:
  void draw();
  void drawFrames(tgfx::Canvas* canvas);
  void drawBackground(tgfx::Canvas* canvas);
  void drawSelectFrame(tgfx::Canvas* canvas, int onScreen, int frameWidth);
  void drawSelect(tgfx::Canvas* canvas, std::pair<uint32_t, uint32_t>& range, int onScreen,
                  int frameWidth, uint32_t color);
  QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData*) override;

 private:
  Worker* worker = nullptr;
  const FrameData* frames = nullptr;
  ViewData* viewData = nullptr;

  uint64_t frameTarget;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;

  //Left button
  float viewOffset = 0.0f;
  float placeWidth = 50.f;

  QPoint lastRightDragPos;
};
}  // namespace inspector