/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

  Q_INVOKABLE void updateTransform(qreal zoomLevel, QPointF panOffset);
  Q_INVOKABLE void onClicked();

 protected:
  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;

 private:
  int currentDrawerIndex = 0;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<drawers::AppHost> appHost = nullptr;
  float zoom = 1.0f;
  QPointF offset = {0, 0};

  void createAppHost();
  void draw();

 private Q_SLOTS:
  void onSceneGraphInvalidated();
};
}  // namespace hello2d
