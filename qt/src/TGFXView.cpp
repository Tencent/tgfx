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
}

void TGFXView::updateTransform(qreal zoomLevel, QPointF panOffset) {
  float clampedZoom = std::max(0.001f, std::min(1000.0f, static_cast<float>(zoomLevel)));
  zoom = clampedZoom;
  offset = panOffset;
  update();
}

void TGFXView::onClicked() {
  currentDrawerIndex++;
  zoom = 1.0f;
  offset = QPointF(0, 0);
  update();
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
  if (tgfxWindow->getSurface(nullptr) == nullptr ||
      tgfxWindow->getSurface(nullptr)->width() != screenWidth ||
      tgfxWindow->getSurface(nullptr)->height() != screenHeight) {
    tgfxWindow->invalidSize();
    // Clear lastRecording when size changes, as it was created for the old surface size
    lastRecording = nullptr;
    // Mark size as invalidated to force render next frame
    sizeInvalidated = true;
  }

  // Draw and check if content has changed
  bool hasContentChanged = draw();

  // Schedule next update only if content is still changing
  if (hasContentChanged) {
    update();
  }

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

bool TGFXView::draw() {
  // ========== All DisplayList updates BEFORE locking device ==========

  // Switch sample when drawIndex changes
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

  // Calculate base scale and offset using cached surface size
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    bool zoomChanged = (zoom != lastAppliedZoom);
    bool offsetChanged =
        (offset.x() != lastAppliedOffset.x() || offset.y() != lastAppliedOffset.y());
    if (zoomChanged || offsetChanged) {
      static constexpr float DESIGN_SIZE = 720.0f;
      auto scaleX = static_cast<float>(lastSurfaceWidth) / DESIGN_SIZE;
      auto scaleY = static_cast<float>(lastSurfaceHeight) / DESIGN_SIZE;
      auto baseScale = std::min(scaleX, scaleY);
      auto scaledSize = DESIGN_SIZE * baseScale;
      auto baseOffsetX = (static_cast<float>(lastSurfaceWidth) - scaledSize) * 0.5f;
      auto baseOffsetY = (static_cast<float>(lastSurfaceHeight) - scaledSize) * 0.5f;

      displayList.setZoomScale(zoom * baseScale);
      displayList.setContentOffset(baseOffsetX + static_cast<float>(offset.x()),
                                   baseOffsetY + static_cast<float>(offset.y()));
      lastAppliedZoom = zoom;
      lastAppliedOffset = offset;
    }
  }

  // Check if content has changed AFTER setting all properties, BEFORE locking device
  bool needsRender = displayList.hasContentChanged() || sizeInvalidated;

  // If no content change AND no pending lastRecording -> skip everything, don't lock device
  if (!needsRender && lastRecording == nullptr) {
    return false;
  }

  // ========== Now lock device for rendering/submitting ==========

  auto device = tgfxWindow->getDevice();
  if (device == nullptr) {
    return false;
  }
  auto context = device->lockContext();
  if (context == nullptr) {
    return false;
  }

  auto surface = tgfxWindow->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return false;
  }

  // Update cached surface size for next frame's calculations
  int newSurfaceWidth = surface->width();
  int newSurfaceHeight = surface->height();
  bool sizeChanged = (newSurfaceWidth != lastSurfaceWidth || newSurfaceHeight != lastSurfaceHeight);
  lastSurfaceWidth = newSurfaceWidth;
  lastSurfaceHeight = newSurfaceHeight;

  // If surface size just changed, update zoomScale/contentOffset now
  if (sizeChanged && lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    static constexpr float DESIGN_SIZE = 720.0f;
    auto scaleX = static_cast<float>(lastSurfaceWidth) / DESIGN_SIZE;
    auto scaleY = static_cast<float>(lastSurfaceHeight) / DESIGN_SIZE;
    auto baseScale = std::min(scaleX, scaleY);
    auto scaledSize = DESIGN_SIZE * baseScale;
    auto baseOffsetX = (static_cast<float>(lastSurfaceWidth) - scaledSize) * 0.5f;
    auto baseOffsetY = (static_cast<float>(lastSurfaceHeight) - scaledSize) * 0.5f;
    displayList.setZoomScale(zoom * baseScale);
    displayList.setContentOffset(baseOffsetX + static_cast<float>(offset.x()),
                                 baseOffsetY + static_cast<float>(offset.y()));
    lastAppliedZoom = zoom;
    lastAppliedOffset = offset;
    needsRender = true;
  }

  // Clear sizeInvalidated flag
  sizeInvalidated = false;

  // Track if we submitted anything this frame
  bool didSubmit = false;

  // Case 1: No content change BUT have pending lastRecording -> only submit lastRecording
  if (!needsRender) {
    context->submit(std::move(lastRecording));
    tgfxWindow->present(context);
    didSubmit = true;
    device->unlock();
    return didSubmit;
  }

  // Case 2: Content changed -> render new content
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto pixelRatio = window()->devicePixelRatio();
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), pixelRatio);

  // Render DisplayList
  displayList.render(surface.get(), false);

  // Delayed one-frame present mode
  auto recording = context->flush();
  if (lastRecording) {
    // Normal delayed mode: submit last frame, save current for next
    context->submit(std::move(lastRecording));
    tgfxWindow->present(context);
    didSubmit = true;
    lastRecording = std::move(recording);
  } else if (recording) {
    // No lastRecording (first frame or after size change): submit current directly
    context->submit(std::move(recording));
    tgfxWindow->present(context);
    didSubmit = true;
  }

  device->unlock();

  // Return true if we submitted or have pending recording
  return didSubmit || lastRecording != nullptr;
}
}  // namespace hello2d
