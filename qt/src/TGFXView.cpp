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

#include "TGFXView.h"
#include <QApplication>
#include <QFileInfo>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QThread>
#include "hello2d/LayerBuilder.h"

namespace hello2d {
TGFXView::TGFXView(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::LeftButton);
  setAcceptHoverEvents(true);
  setAcceptTouchEvents(true);
  setFocus(true);
  createAppHost();
  displayList.setRenderMode(tgfx::RenderMode::Tiled);
  displayList.setAllowZoomBlur(true);
  displayList.setMaxTileCount(512);
  updateDisplayList();
}

void TGFXView::updateTransform(qreal zoomLevel, QPointF panOffset) {
  float clampedZoom = std::max(0.001f, std::min(1000.0f, static_cast<float>(zoomLevel)));
  zoom = clampedZoom;
  offset = panOffset;
  updateDisplayList();
  update();
}

void TGFXView::onClicked() {
  currentDrawerIndex++;
  zoom = 1.0f;
  offset = QPointF(0, 0);
  updateDisplayList();
  update();
}

void TGFXView::updateDisplayList() {
  auto numBuilders = hello2d::LayerBuilder::Count();
  auto index = (currentDrawerIndex % numBuilders);
  if (index != lastDrawIndex || !contentLayer) {
    auto builder = hello2d::LayerBuilder::GetByIndex(index);
    if (builder) {
      contentLayer = builder->buildLayerTree(appHost.get());
      if (contentLayer) {
        displayList.root()->removeChildren();
        displayList.root()->addChild(contentLayer);
      }
    }
    lastDrawIndex = index;
  }

  applyTransform();
}

void TGFXView::applyTransform() {
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    if (contentLayer) {
      hello2d::LayerBuilder::ApplyCenteringTransform(contentLayer,
                                                     static_cast<float>(lastSurfaceWidth),
                                                     static_cast<float>(lastSurfaceHeight));
    }
    displayList.setZoomScale(zoom);
    displayList.setContentOffset(static_cast<float>(offset.x()), static_cast<float>(offset.y()));
  }
}

QSGNode* TGFXView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  if (!tgfxWindow) {
    tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
    connect(window(), SIGNAL(sceneGraphInvalidated()), this, SLOT(onSceneGraphInvalidated()),
            Qt::DirectConnection);
  }
  auto pixelRatio = window()->devicePixelRatio();
  auto screenWidth = static_cast<int>(ceil(width() * pixelRatio));
  auto screenHeight = static_cast<int>(ceil(height() * pixelRatio));
  if (tgfxWindow->getSurface(nullptr) == nullptr ||
      tgfxWindow->getSurface(nullptr)->width() != screenWidth ||
      tgfxWindow->getSurface(nullptr)->height() != screenHeight) {
    lastSurfaceWidth = screenWidth;
    lastSurfaceHeight = screenHeight;
    applyTransform();
    tgfxWindow->invalidSize();
    lastRecording = nullptr;
    sizeInvalidated = true;
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
  tgfxWindow = nullptr;
}

void TGFXView::createAppHost() {
  appHost = std::make_unique<hello2d::AppHost>();
  auto rootPath = QApplication::applicationDirPath();
  rootPath = QFileInfo(rootPath + "/../../").absolutePath();
  auto imagePath = rootPath + "/resources/assets/bridge.jpg";
  auto image = tgfx::Image::MakeFromFile(std::string(imagePath.toLocal8Bit()));
  appHost->addImage("bridge", image);
  imagePath = rootPath + "/resources/assets/tgfx.png";
  appHost->addImage("TGFX", tgfx::Image::MakeFromFile(std::string(imagePath.toLocal8Bit())));
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
  if (!displayList.hasContentChanged() && lastRecording == nullptr) {
    return;
  }

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

  // Sync surface size for DPI changes.
  bool surfaceResized = sizeInvalidated;
  sizeInvalidated = false;

  if (surface->width() != lastSurfaceWidth || surface->height() != lastSurfaceHeight) {
    lastSurfaceWidth = surface->width();
    lastSurfaceHeight = surface->height();
    applyTransform();
    surfaceResized = true;
  }

  bool needsRender = displayList.hasContentChanged();

  if (!needsRender) {
    if (lastRecording) {
      context->submit(std::move(lastRecording));
      tgfxWindow->present(context);
      lastRecording = nullptr;
    }
    device->unlock();
    return;
  }

  auto canvas = surface->getCanvas();
  canvas->clear();
  auto pixelRatio = static_cast<float>(window()->devicePixelRatio());
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), pixelRatio);

  displayList.render(surface.get(), false);

  auto recording = context->flush();

  if (surfaceResized) {
    // When resized, submit current frame immediately (no delay)
    if (recording) {
      context->submit(std::move(recording));
      tgfxWindow->present(context);
    }
    // Clear lastRecording since we need to restart the delay cycle after resize
    lastRecording = nullptr;
  } else {
    // Normal case: delayed one-frame present
    std::swap(lastRecording, recording);

    if (recording) {
      context->submit(std::move(recording));
      tgfxWindow->present(context);
    }
  }

  device->unlock();

  if (lastRecording != nullptr) {
    update();
  }
}
}  // namespace hello2d
