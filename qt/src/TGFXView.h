/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <QOpenGLContext>
#include <QQuickItem>
#include "drawers/AppHost.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"

namespace hello2d {
class TGFXView : public QQuickItem {
  Q_OBJECT
 public:
  explicit TGFXView(QQuickItem* parent = nullptr);
  Q_INVOKABLE void handlePinch(qreal scaleDelta, QPointF center);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;

 private:
  int lastDrawIndex = 0;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<drawers::AppHost> appHost = nullptr;
  float zoom = 1.0f;
  QPointF offset = {0, 0};
  bool dragging = false;
  QPoint lastMousePos;

  void createAppHost();
  void draw();

  Q_SLOT
  void onSceneGraphInvalidated();
};
}  // namespace hello2d
