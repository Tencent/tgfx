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

#include "TGFXView.h"
#include <QApplication>
#include <QFileInfo>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QThread>
#include "drawers/Drawer.h"

namespace hello2d {
TGFXView::TGFXView(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::LeftButton);
  setAcceptHoverEvents(true);
  setAcceptTouchEvents(true);
  setFocus(true);
  createAppHost();
}

void TGFXView::handlePinch(qreal scaleDelta, QPointF center) {
  float oldZoom = zoom;
  float newZoom = std::max(0.001f, std::min(1000.0f, oldZoom * (float)scaleDelta));
  float px = (float)center.x();
  float py = (float)center.y();
  offset.setX((offset.x() - px) * (newZoom / oldZoom) + px);
  offset.setY((offset.y() - py) * (newZoom / oldZoom) + py);
  zoom = newZoom;
  update();
}

void TGFXView::onMouseClicked(qreal, qreal) {
  lastDrawIndex++;
  offset = {0, 0};
  zoom = 1.0f;
  update();
}

void TGFXView::wheelEvent(QWheelEvent* event) {
  bool isZoom =
      (event->modifiers() & Qt::ControlModifier) || (event->modifiers() & Qt::MetaModifier);
  qreal px = event->position().x();
  qreal py = event->position().y();
  if (isZoom) {
    float factor = (float)std::exp(event->angleDelta().y() / 300.0);
    float oldZoom = zoom;
    float newZoom = std::max(0.001f, std::min(1000.0f, oldZoom * factor));
    offset.setX((offset.x() - px) * (newZoom / oldZoom) + px);
    offset.setY((offset.y() - py) * (newZoom / oldZoom) + py);
    zoom = newZoom;
  } else {
    offset.rx() += event->angleDelta().x();
    offset.ry() += event->angleDelta().y();
  }
  update();
  event->accept();
}

QSGNode* TGFXView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  if (!tgfxWindow) {
    // Do not set singleBufferMode to true if you want to draw in a different thread than the QSG
    // render thread.
    tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
    connect(window(), SIGNAL(sceneGraphInvalidated()), this, SLOT(onSceneGraphInvalidated()),
            Qt::DirectConnection);
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
  auto node = static_cast<QSGImageNode*>(oldNode);
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

void TGFXView::onSceneGraphInvalidated() {
  disconnect(window(), SIGNAL(sceneGraphInvalidated()), this, SLOT(onSceneGraphInvalidated()));
  // Release the tgfxWindow on the QSG render thread or call tgfxWindow->moveToThread() to move it. Otherwise,
  // destroying the tgfxWindow in the main thread will cause an error.
  tgfxWindow = nullptr;
}

void TGFXView::createAppHost() {
  appHost = std::make_unique<drawers::AppHost>();
  auto rootPath = QApplication::applicationDirPath();
  rootPath = QFileInfo(rootPath + "/../../").absolutePath();
  auto imagePath = rootPath + "/resources/assets/bridge.jpg";
  auto image = tgfx::Image::MakeFromFile(std::string(imagePath.toLocal8Bit()));
  appHost->addImage("bridge", image);
#ifdef __APPLE__
  auto defaultTypeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
  auto emojiTypeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
#else
  auto defaultTypeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
  auto emojiPath = rootPath + R"(\resources\font\NotoColorEmoji.ttf)";
  auto emojiTypeface = tgfx::Typeface::MakeFromPath(std::string(emojiPath.toLocal8Bit()));
#endif
  appHost->addTypeface("default", defaultTypeface);
  appHost->addTypeface("emoji", emojiTypeface);
}

void TGFXView::draw() {
  appHost->updateZoomAndOffset(zoom, tgfx::Point((float)offset.x(), (float)offset.y()));
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
  canvas->save();
  auto numDrawers = drawers::Drawer::Count() - 1;
  auto index = (lastDrawIndex % numDrawers) + 1;
  auto drawer = drawers::Drawer::GetByName("GridBackground");
  drawer->draw(canvas, appHost.get());
  drawer = drawers::Drawer::GetByIndex(index);
  drawer->draw(canvas, appHost.get());
  canvas->restore();
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}
}  // namespace hello2d
