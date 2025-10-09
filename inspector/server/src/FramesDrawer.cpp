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

#include "FramesDrawer.h"
#include <qevent.h>
#include <QSGImageNode>
#include <QToolTip>
#include <iostream>
#include "Draw.h"
#include "TimePrint.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"

namespace inspector {
FramesDrawer::FramesDrawer(QQuickItem* parent)
    : QQuickItem(parent), frameTarget(1000 * 1000 * 1000 / 60),
      appHost(AppHostSingleton::GetInstance()) {
  setFlag(ItemHasContents, true);
  setFlag(ItemAcceptsInputMethod, true);
  setFlag(ItemIsFocusScope, true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
}

uint32_t GetFrameColor(uint64_t time, uint64_t target) {
  return time > target * 2   ? 0xFF8888FF
         : time > target     ? 0xFF88FFFF
         : time > target / 2 ? 0xFF88FF88
                             : 0xFFFFCC88;
}

void FramesDrawer::draw() {
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
  canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
  drawRect(canvas, 0, 0, static_cast<float>(width()), static_cast<float>(height()), 0xFF2E2E2E);
  drawFrames(canvas);
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}

void FramesDrawer::drawSelect(tgfx::Canvas* canvas, std::pair<uint32_t, uint32_t>& range,
                              int onScreen, int frameWidth, uint32_t color) {
  auto transparentColor = color & 0x55FFFFFF;
  if (range.second > viewData->frameStart &&
      range.first < viewData->frameStart + static_cast<uint32_t>(onScreen)) {
    auto x1 =
        std::min(onScreen * frameWidth, int(range.second - viewData->frameStart) * frameWidth);
    auto x0 = std::max(0, int(range.first - viewData->frameStart) * frameWidth);
    if (x0 == x1) {
      x1 = x0 + frameWidth;
    }
    auto fx1 = static_cast<float>(x1);
    auto fx0 = static_cast<float>(x0);
    auto h = static_cast<float>(height());
    if (x1 - x0 >= 3) {
      drawRect(canvas, 2.f + fx0, 0, fx1 - fx0, h, transparentColor);
      auto p1 = tgfx::Point{2.f + fx0, -1.f};
      auto p2 = tgfx::Point{2.f + fx0, h - 1.f};
      auto p3 = tgfx::Point{fx1, -1.f};
      auto p4 = tgfx::Point{fx1, h - 1.f};

      drawLine(canvas, p1, p2, color);
      drawLine(canvas, p3, p4, color);
    } else {
      drawRect(canvas, 2.f + fx0, 0, fx1 - fx0, h, transparentColor);
    }
  }
}

void FramesDrawer::drawSelectFrame(tgfx::Canvas* canvas, int onScreen, int frameWidth) {
  auto range = std::make_pair(viewData->selectFrame, viewData->selectFrame);
  drawSelect(canvas, range, onScreen, frameWidth, 0xFF7259A3);
}

void FramesDrawer::drawFrames(tgfx::Canvas* canvas) {
  canvas->translate(viewOffset, 0);
  drawBackground(canvas);

  if (worker->getFrameCount() == 0) {
    return;
  }
  const auto w = static_cast<int>(width() - placeWidth);
  const auto frameWidth = 4;
  const auto total = worker->getFrameCount();
  const auto onScreen = (w - 2) / frameWidth;

  int i = 0;
  uint32_t idx = 0;
  while (i < onScreen && viewData->frameStart + idx < total) {
    auto frameTime = worker->getFrameTime(*frames, size_t(viewData->frameStart + idx));
    const auto currentHeight =
        std::min(MaxFrameTime, frameTime) / float(MaxFrameTime) * float(height() - 2);
    const auto frameHeight = std::max(1.f, currentHeight);
    auto color = GetFrameColor(static_cast<uint64_t>(frameTime), frameTarget);

    if (frameWidth != 1) {
      auto p1 = tgfx::Point{2.f + i * frameWidth, (float)height() - 1.f - frameHeight};
      auto p2 =
          tgfx::Point{(float)frameWidth + i * frameWidth - p1.x, (float)height() - 1.f - p1.y};
      drawRect(canvas, p1, p2, color);
    } else {
      auto p1 = tgfx::Point{1.f + i, (float)height() - 2 - frameHeight};
      auto p2 = tgfx::Point{1.f + i, (float)height() - 2};
      drawLine(canvas, p1, p2, color);
    }
    i++;
    idx += 1;
  }

  drawSelectFrame(canvas, onScreen, frameWidth);
}

QSGNode* FramesDrawer::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  auto node = static_cast<QSGImageNode*>(oldNode);
  if (!tgfxWindow) {
    tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
  }
  auto pixelRatio = window()->devicePixelRatio();
  auto screenWidth = static_cast<int>(ceil(width() * pixelRatio));
  auto screenHeight = static_cast<int>(ceil((float)height() * pixelRatio));
  auto sizeChanged =
      appHost->updateScreen(screenWidth, screenHeight, static_cast<float>(pixelRatio));
  if (sizeChanged) {
    tgfxWindow->invalidSize();
  }
  draw();
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

void FramesDrawer::drawBackground(tgfx::Canvas* canvas) {
  const auto dpos = tgfx::Point{0.f, 0.f};
  const auto h = static_cast<float>(height());
  const auto w = static_cast<float>(width());
  auto fontSize = 12.f;

  auto p1 = tgfx::Point{w - placeWidth, 0};
  auto p2 = tgfx::Point{placeWidth, h};
  auto textXStart = p1.x + p2.x / 2;
  drawRect(canvas, p1, p2, 0x66BB7DC8);

  p1 = dpos + tgfx::Point{0, round(h - h * frameTarget * 2 / MaxFrameTime)};
  p2 = dpos + tgfx::Point{w, round(h - h * frameTarget * 2 / MaxFrameTime)};
  drawLine(canvas, p1, p2, 0x442222DD);
  auto textFps = "30FPS";
  auto textBounds = getTextSize(appHost.get(), textFps, 0, fontSize);
  auto textPoint = tgfx::Point{textXStart - textBounds.width() / 2, p2.y + textBounds.height() / 2};
  drawTextWithBlackRect(canvas, appHost.get(), textFps, textPoint.x, textPoint.y, 0xFF2222DD,
                        fontSize);

  p1 = dpos + tgfx::Point{0, round(h - h * frameTarget / MaxFrameTime)};
  p2 = dpos + tgfx::Point{w, round(h - h * frameTarget / MaxFrameTime)};
  drawLine(canvas, p1, p2, 0x4422DDDD);
  textFps = "60FPS";
  textBounds = getTextSize(appHost.get(), textFps, 0, fontSize);
  textPoint = tgfx::Point{textXStart - textBounds.width() / 2, p2.y + textBounds.height() / 2};
  drawTextWithBlackRect(canvas, appHost.get(), textFps, textPoint.x, textPoint.y, 0xFF22DDDD,
                        fontSize);

  p1 = dpos + tgfx::Point{0, round(h - h * frameTarget / 2 / MaxFrameTime)};
  p2 = dpos + tgfx::Point{w, round(h - h * frameTarget / 2 / MaxFrameTime)};
  drawLine(canvas, p1, p2, 0x4422DD22);
  textFps = "120FPS";
  textBounds = getTextSize(appHost.get(), textFps, 0, fontSize);
  textPoint = tgfx::Point{textXStart - textBounds.width() / 2, p2.y + textBounds.height() / 2};
  drawTextWithBlackRect(canvas, appHost.get(), textFps, textPoint.x, textPoint.y, 0xFF22DD22,
                        fontSize);
}

void FramesDrawer::wheelEvent(QWheelEvent* event) {
  auto wheel = event->angleDelta().y();
  auto frameCount = worker->getFrameCount();
  if (wheel > 0 && viewData->frameStart < frameCount) {
    ++viewData->frameStart;
    update();
  } else if (wheel < 0 && viewData->frameStart > 0) {
    --viewData->frameStart;
    update();
  }

  event->accept();
}

void FramesDrawer::mousePressEvent(QMouseEvent* event) {
  auto pos = event->pos();

  if (event->button() == Qt::LeftButton) {
    const int frameWidth = viewData->frameWidth;
    const auto total = worker->getFrameCount();
    const auto mx = pos.x();
    if (mx > 0 && mx < width() - 1) {
      const auto mo = mx - 1;
      const auto off = static_cast<uint32_t>(mo / frameWidth);
      const auto sel = viewData->frameStart + off;
      if (sel < total) {
        viewData->selectFrame = sel;
        update();
        Q_EMIT selectFrame();
      }
    }
    event->accept();
    return;
  }

  if (event->button() == Qt::RightButton) {
    lastRightDragPos = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QQuickItem::mousePressEvent(event);
}

void FramesDrawer::mouseMoveEvent(QMouseEvent* event) {
  const auto pos = event->pos();
  const int frameWidth = viewData->frameWidth;

  if (event->buttons() & Qt::RightButton) {
    const auto delta = (event->pos().x() - lastRightDragPos.x());
    if (abs(delta) >= frameWidth) {
      const auto d = delta / frameWidth;
      viewData->frameStart =
          static_cast<uint32_t>(std::max(0, static_cast<int32_t>(viewData->frameStart) - d));
      lastRightDragPos = pos;
      lastRightDragPos.setX(lastRightDragPos.x() + d * frameWidth - delta);
    }
    update();
    event->accept();
  }
}

void FramesDrawer::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton) {
    setCursor(Qt::ArrowCursor);
    event->accept();
    return;
  }
  QQuickItem::mouseReleaseEvent(event);
}
}  // namespace inspector
