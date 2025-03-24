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

#include "FramesView.h"
#include <qevent.h>
#include <QSGImageNode>
#include <QToolTip>
#include "TimelineView.h"
#include "TracyPrint.hpp"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"

FramesView::FramesView(QQuickItem* parent)
  : QQuickItem(parent), appHost(AppHostInstance::GetAppHostInstance()) {
  setFlag(ItemHasContents, true);
  setFlag(ItemAcceptsInputMethod, true);
  setFlag(ItemIsFocusScope, true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
}

FramesView::~FramesView() {
}

uint64_t FramesView::getFrameNumber(const tracy::FrameData& frameData, uint64_t i) {
  if (frameData.name == 0) {
    const auto offset = worker->GetFrameOffset();
    if (offset == 0) {
      return i;
    } else {
      return i + offset - 1;
    }
  }
  return i + 1;
}

uint32_t GetFrameColor(uint64_t time, uint64_t target) {
  return time > target * 2   ? 0xFF8888FF
         : time > target     ? 0xFF88FFFF
         : time > target / 2 ? 0xFF88FF88
                             : 0xFFFFCC88;
}

int GetFrameWidth(int frameScale) {
  return frameScale == 0 ? 4 : (frameScale < 0 ? 6 : 1);
}

int GetFrameGroup(int frameScale) {
  return frameScale < 2 ? 1 : (1 << (frameScale - 1));
};

void FramesView::draw() {
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

void FramesView::drawSelect(tgfx::Canvas* canvas, std::pair<int, int>& range, int onScreen, int frameWidth, int group, uint32_t color) {
  auto transparentColor = color & 0x55FFFFFF;
  if (range.second > viewData->frameStart &&
        range.first < viewData->frameStart + onScreen * group) {
    auto x1 = std::min(onScreen * frameWidth,
                       (range.second - viewData->frameStart) * frameWidth / group);
    auto x0 = std::max(0, (range.first - viewData->frameStart) * frameWidth / group);
    if (x0 == x1) x1 = x0 + frameWidth;
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

void FramesView::drawSelectFrame(tgfx::Canvas* canvas, int onScreen, int frameWidth, int group) {
  auto range = worker->GetFrameRange(*frames, viewData->zvStart, viewData->zvEnd);
  if (range.first != -1) {
    selectedStartFrame = range.first;
    selectedEndFrame = range.second;
  }
  drawSelect(canvas, range, onScreen, frameWidth, group, 0xFF7259A3);
}

void FramesView::drawFrames(tgfx::Canvas* canvas) {
  assert(worker->GetFrameCount(*frames) != 0);
  canvas->translate(viewOffset, 0);

  const int frameWidth = GetFrameWidth(viewData->frameScale);
  const int group = GetFrameGroup(viewData->frameScale);
  const int total = static_cast<int>(worker->GetFrameCount(*frames));
  const int onScreen = (static_cast<int>(width()) - 2) / frameWidth;

  if (*viewMode != ViewMode::Paused) {
    viewData->frameStart = (total < onScreen * group) ? 0 : total - onScreen * group;
    if (*viewMode == ViewMode::LastFrames) {
      setViewToLastFrames();
    } else {
      assert(*viewMode == ViewMode::LastRange);
      const auto delta = worker->GetLastTime() - viewData->zvEnd;
      if (delta != 0) {
        viewData->zvStart += delta;
        viewData->zvEnd += delta;
      }
    }
  }

  drawBackground(canvas);
  int i = 0, idx = 0;
  while (i < onScreen && viewData->frameStart + idx < total) {
    auto frameTime = worker->GetFrameTime(*frames, size_t(viewData->frameStart + idx));
    if (group > 1) {
      const auto g = std::min(group, total - (viewData->frameStart + idx));
      for (int j = 1; j < g; j++) {
        frameTime = std::max(frameTime,
                             worker->GetFrameTime(*frames, size_t(viewData->frameStart + idx + j)));
      }
    }
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
    idx += group;
  }

  drawSelectFrame(canvas, onScreen, frameWidth, group);
}

void FramesView::setViewToLastFrames() {
  auto total = worker->GetFrameCount(*frames);

  viewData->zvStart = worker->GetFrameBegin(*frames, (size_t)std::max(0, int(total) - 4));
  if (total == 1) {
    viewData->zvEnd = worker->GetLastTime();
  } else {
    viewData->zvEnd = worker->GetFrameBegin(*frames, total - 1);
  }
  if (viewData->zvEnd == viewData->zvStart) {
    viewData->zvEnd = worker->GetLastTime();
  }
}

QSGNode* FramesView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
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

void FramesView::drawBackground(tgfx::Canvas* canvas) {
  const auto dpos = tgfx::Point{0.f, 0.f};

  const uint64_t frameTarget = 1000 * 1000 * 1000 / viewData->frameTarget;

  auto p1 =
      dpos +
      tgfx::Point{0, round((float)height() - (float)height() * frameTarget * 2 / MaxFrameTime)};
  auto p2 =
      dpos + tgfx::Point{(float)width(),
                         round((float)height() - (float)height() * frameTarget * 2 / MaxFrameTime)};
  drawLine(canvas, p1, p2, 0x442222DD);

  p1 = dpos + tgfx::Point{0, round((float)height() - float(height() * frameTarget / MaxFrameTime))};
  p2 = dpos + tgfx::Point{(float)width(),
                          round((float)height() - (float)height() * frameTarget / MaxFrameTime)};
  drawLine(canvas, p1, p2, 0x4422DDDD);

  p1 = dpos +
       tgfx::Point{0, round((float)height() - (float)height() * frameTarget / 2 / MaxFrameTime)};
  p2 = dpos + tgfx::Point{(float)width(), round((float)height() -
                                                (float)height() * frameTarget / 2 / MaxFrameTime)};
  drawLine(canvas, p1, p2, 0x4422DD22);
}

void FramesView::wheelEvent(QWheelEvent* event) {
  auto mode = 1;
  auto wheel = event->angleDelta().y();
  if (wheel > 0 && viewData->frameScale < 4) {
    viewData->frameScale += mode;
    update();
  } else if (wheel < 0 && viewData->frameScale > -2) {
    viewData->frameScale -= mode;
    update();
  }

  event->accept();
}

void FramesView::mousePressEvent(QMouseEvent* event) {
  auto pos = event->pos();

  if (event->button() == Qt::LeftButton) {
    const int frameWidth = GetFrameWidth(viewData->frameScale);
    const int group = GetFrameGroup(viewData->frameScale);
    const auto total = worker->GetFrameCount(*frames);

    const auto mx = pos.x();
    if (mx > 0 && mx < width() - 1) {
      const auto mo = mx - 1;
      const auto off = mo * group / frameWidth;
      const int sel = viewData->frameStart + off;

      if (size_t(sel) < total) {
        dragStartFrame = sel;
        isLeftDagging = true;
        *viewMode = ViewMode::Paused;
        viewData->zvStart = worker->GetFrameBegin(*frames, size_t(sel));
        viewData->zvEnd = worker->GetFrameEnd(*frames, size_t(sel + group - 1));
        if (viewData->zvStart == viewData->zvEnd) {
          viewData->zvStart--;
        }
        update();
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

void FramesView::mouseMoveEvent(QMouseEvent* event) {
  const auto pos = event->pos();
  const int frameWidth = GetFrameWidth(viewData->frameScale);
  const int group = GetFrameGroup(viewData->frameScale);
  const auto total = worker->GetFrameCount(*frames);

  if (event->buttons() & Qt::LeftButton) {
    const auto mx = pos.x();
    if (mx > 0 && mx < width() - 1) {

      const auto mo = mx - 1;
      const auto off = mo * group / frameWidth;
      const int sel = viewData->frameStart + off;

      if (static_cast<size_t>(sel) < total) {
        if (sel < dragStartFrame) {
          selectedStartFrame = sel;
          selectedEndFrame = dragStartFrame;
        } else {
          selectedStartFrame = dragStartFrame;
          selectedEndFrame = sel;
        }

        const auto t0 = worker->GetFrameBegin(*frames, size_t(selectedStartFrame));
        const auto t1 = worker->GetFrameEnd(*frames, size_t(selectedEndFrame));
        viewData->zvStart = t0;
        viewData->zvEnd = t1;

        Q_EMIT statRangeChanged(selectedStartFrame, selectedEndFrame, false);
      }
    }
    event->accept();
    return;
  }

  if (event->buttons() & Qt::RightButton) {
    *viewMode = ViewMode::Paused;
    const auto delta = (event->pos().x() - lastRightDragPos.x());
    if (abs(delta) >= frameWidth) {
      const auto d = delta / frameWidth;
      viewData->frameStart = std::max(0, viewData->frameStart - d * group);
      lastRightDragPos = pos;
      lastRightDragPos.setX(lastRightDragPos.x() + d * frameWidth - delta);
    }
    update();
    event->accept();
  }
}

void FramesView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton) {
    setCursor(Qt::ArrowCursor);
    event->accept();
    return;
  }

  if (event->button() == Qt::LeftButton) {
    isLeftDagging = false;
    const auto pos = event->pos();
    const int frameWidth = GetFrameWidth(viewData->frameScale);
    const int group = GetFrameGroup(viewData->frameScale);

    const auto mx = pos.x();
    if (mx > 0 && mx < width() - 1) {
      const auto mo = mx - 1;
      const auto off = mo * group / frameWidth;
      const auto sel = viewData->frameStart + off;

      if (sel == dragStartFrame) {
        selectedStartFrame = sel;
        selectedEndFrame = sel + group - 1;
        int64_t startTime = worker->GetFrameBegin(*frames, static_cast<size_t>(sel));
        int64_t endTime = worker->GetFrameEnd(*frames, static_cast<size_t>(selectedEndFrame));
        viewData->zvStart = startTime;
        viewData->zvEnd = endTime;
        if (viewData->zvStart == viewData->zvEnd) viewData->zvStart--;
        Q_EMIT statRangeChanged(selectedStartFrame, selectedEndFrame, false);
      }
    }
    update();
  }
  QQuickItem::mouseReleaseEvent(event);
}

void FramesView::hoverMoveEvent(QHoverEvent* event) {
  if (!event || !frames || !worker) {
    QQuickItem::hoverMoveEvent(event);
    return;
  }

  const int frameWidth = GetFrameWidth(viewData->frameScale);
  const int group = GetFrameGroup(viewData->frameScale);
  const auto mouseX = int(event->position().x());
  const auto adjustedX = mouseX - int(viewOffset);
  const auto offset = adjustedX * group / frameWidth;
  const auto total = int(worker->GetFrameCount(*frames));
  const int sel = viewData->frameStart + offset;
  const auto sele = static_cast<size_t>(sel);

  if (sel >= 0 && sel < total) {
    QString text;
    if (group > 1) {
      auto frameTime = worker->GetFrameTime(*frames, size_t(sel));
      auto g = std::min(group, total - sel);
      for (int j = 1; j < g; ++j) {
        frameTime = std::max(frameTime, worker->GetFrameTime(*frames, sele + size_t(j)));
      }
      text = QString("Frames:%1 - %2(%3)\nMax Frame Time:%4(%5FPS)\n")
                 .arg(sel)
                 .arg(sel + g - 1)
                 .arg(g)
                 .arg(tracy::TimeToString(frameTime))
                 .arg(1000000000.0 / frameTime);
    } else {
      const auto frameNumber = getFrameNumber(*frames, sele);
      frameHover = frameNumber;
      if (frames->name == 0) {
        const auto frameTime = worker->GetFrameTime(*frames, sele);
        const auto strFrameTime = tracy::TimeToString(frameTime);
        if (sel == 0) {
          text = QString("Tracy Initialization\nTime:%1\n").arg(strFrameTime);
        } else if (!worker->IsOnDemand()) {
          text = QString("Frames:%1\nFrame Time:%2(%3 FPS)\n")
                     .arg(frameNumber)
                     .arg(strFrameTime)
                     .arg(1000000000.0 / frameTime);
        } else if (sel == 1) {
          text = QString("Missed frames\nTime:%1")
                     .arg(tracy::TimeToString(worker->GetFrameTime(*frames, 1)));
        } else {
          text = QString("Frames:%1\nFrame Time:%2(%3 FPS)\n")
                     .arg(frameNumber)
                     .arg(strFrameTime)
                     .arg(1000000000.0 / frameTime);
        }
      }
    }
    text += QString("Time from start of program:%1\nDrawCall:%2\nTrangles:%3")
                .arg(tracy::TimeToStringExact(worker->GetFrameBegin(*frames, sele)))
                .arg(worker->GetFrameDrawCall(*frames, sele))
                .arg(worker->GetFrameTrangles(*frames, sele));

    QPoint globalPos = QCursor::pos();
    QToolTip::showText(globalPos, text, nullptr);

  } else {
    QToolTip::hideText();
  }
  QQuickItem::hoverMoveEvent(event);
}
