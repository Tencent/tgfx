/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "hello2d/AppHost.h"
#include "tgfx/gpu/Recording.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"
#include "tgfx/layers/DisplayList.h"

namespace hello2d {
class TGFXView : public QQuickItem {
  Q_OBJECT
 public:
  explicit TGFXView(QQuickItem* parent = nullptr);

  Q_INVOKABLE void updateTransform(qreal zoomLevel, QPointF panOffset);
  Q_INVOKABLE void onClicked();

 protected:
  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;

 private:
  int currentDrawerIndex = 0;
  int lastDrawIndex = -1;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<hello2d::AppHost> appHost = nullptr;
  tgfx::DisplayList displayList = {};
  std::shared_ptr<tgfx::Layer> contentLayer = nullptr;
  std::unique_ptr<tgfx::Recording> lastRecording = nullptr;
  float zoom = 1.0f;
  QPointF offset = {0, 0};
  int lastSurfaceWidth = 0;
  int lastSurfaceHeight = 0;
  bool presentImmediately = true;

  void createAppHost();
  void updateLayerTree();
  void updateZoomScaleAndOffset();
  void applyCenteringTransform();
  void draw();
 private Q_SLOTS:
  void onSceneGraphInvalidated();
};
}  // namespace hello2d
