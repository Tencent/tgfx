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
#include <QStringList>
#include <QVector>
#include <memory>
#include "AppHost.h"
#include "ViewData.h"
#include "Worker.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"

namespace inspector {
class TextureListDrawer : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(int imageLabel WRITE setImageLabel)
  Q_PROPERTY(Worker* worker READ getWorker WRITE setWorker)
  Q_PROPERTY(ViewData* viewData READ getViewData WRITE setViewData)
 public:
  explicit TextureListDrawer(QQuickItem* parent = nullptr);
  ~TextureListDrawer() override = default;

 Worker* getWorker() const {
    return worker;
 }
 void setWorker(Worker* worker) {
     this->worker = worker;
 }

 ViewData* getViewData() const {
   return viewData;
 }
 void setViewData(ViewData* viewData) {
   this->viewData = viewData;
 }

 void setImageLabel(int label) {
   _label = label;
 }

  Q_SIGNAL void selectedImage(std::shared_ptr<tgfx::Image> image);

  void updateImageData();

 protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
  void mousePressEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

 private:
  void updateLayout();
  void draw();
  int itemAtPosition(float y) const;

private:
  Worker* worker = nullptr;
  ViewData* viewData = nullptr;
  int _label = 0;
  std::vector<tgfx::Rect> squareRects;
  std::vector<std::shared_ptr<tgfx::Image>> images;
  bool layoutDirty = true;
  float scrollOffset = 0.f;

  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
};
}  // namespace inspector
