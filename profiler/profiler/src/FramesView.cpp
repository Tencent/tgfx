/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <sys/socket.h>
#include <tgfx/gpu/opengl/qt/QGLWindow.h>
#include <QSGImageNode>
#include <QToolTip>
#include "TracyPrint.hpp"
#include "TimelineView.h"


FramesView::FramesView(QQuickItem* parent): QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setFlag(ItemAcceptsInputMethod,true);
  setFlag(ItemIsFocusScope,true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
  createAppHost();
}

FramesView::~FramesView() {

}

void FramesView::initView() {
  // setStyleSheet("background-color: rgb(77, 79, 73)");
}

uint64_t FramesView::getFrameNumber(const tracy::FrameData& frameData, int i) {
  if( frameData.name == 0 ) {
    const auto offset = worker->GetFrameOffset();
    if(offset == 0) {
      return i;
    }
    else {
      return i + offset - 1;
    }
  }
  return i + 1;
}

uint32_t GetFrameColor(uint64_t time, uint64_t target) {
  return time > target * 2 ? 0xFF2222DD :
         time > target     ? 0xFF22DDDD :
         time > target / 2 ? 0xFF22DD22 : 0xFFDD9900;
}

//timeRange slot utility.
int64_t FramesView::findFramesfromTime(int64_t time) {
  if (!frames || !worker) return -1;

  const int total = worker->GetFrameCount(*frames);
  int left = 0;
  int right = total - 1;

  while (left <= right) {
    int mid = (left + right) / 2;
    int64_t frameStart = worker->GetFrameBegin(*frames, mid);
    int64_t frameEnd = worker->GetFrameEnd(*frames, mid);

    if (time >= frameStart && time <= frameEnd) {
      return mid;
    } else if (time < frameStart) {
      right = mid - 1;
    } else {
      left = mid + 1;
    }
  }
  return left < total ? left : total - 1;
}

void FramesView::setSelection(int startFrame, int endFrame) {
  if (startFrame == selectedStartFrame && endFrame == selectedEndFrame) return;

  selectedStartFrame = std::max(startFrame,rangeFrameStart);
  selectedEndFrame = std::min(endFrame,rangeFrameEnd);

  if (m_timelineView ) {
    int64_t startTime = worker->GetFrameBegin(*frames, startFrame);
    int64_t endTime = worker->GetFrameEnd(*frames, endFrame);
    m_timelineView->setTimeSelection(startTime, endTime);
  }

  update();

}

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
  drawRect(canvas, 0, 0, width(), height(), 0xFF000000);
  drawBackground(canvas);

  canvas->save();
  canvas->translate(viewOffset,0);

  drawFrames(canvas);

  canvas->restore();
  drawSelection(canvas);
  canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();

}

void FramesView::drawFrames(tgfx::Canvas* canvas) {
  if (!frames) {
    frames = worker->GetFramesBase();
    if (!frames) {
      return;
    }
  }

  drawBackground(canvas);
  assert(worker->GetFrameCount(*frames) != 0);

  const int frameWidth = GetFrameWidth(viewData->frameScale);
  const int group = GetFrameGroup(viewData->frameScale);
  const int total = worker->GetFrameCount(*frames);
  const int onScreen = (width() - 2) / frameWidth;
  if (*viewMode != ViewMode::Paused) {
    viewData->frameStart = (total < onScreen * group) ? 0 : total - onScreen * group;
    if (*viewMode == ViewMode::LastFrames) {
      setViewToLastFrames();
    }
    else {
      assert( *viewMode == ViewMode::LastRange );
      const auto delta = worker->GetLastTime() - viewData->zvEnd;
      if( delta != 0 )
      {
        viewData->zvStart += delta;
        viewData->zvEnd += delta;
      }
    }
  }
  int i = 0, idx = 0;
  while (i < onScreen && viewData->frameStart + idx < total) {
    auto frameTime = worker->GetFrameTime(*frames, viewData->frameStart + idx);
    const auto frameHeight = std::max(1.f, float( std::min<uint64_t>(MaxFrameTime, frameTime)) / MaxFrameTime * (((float)height() - 2)));
    auto color = GetFrameColor(frameTime, frameTarget);

    if (frameWidth != 1) {
      auto p1 = tgfx::Point(2 + i * frameWidth, (float)height() - 1 - frameHeight);
      auto p2 = tgfx::Point(frameWidth + i * frameWidth - p1.x, (float)height() - 1 - p1.y);
      drawRect(canvas, p1, p2, color);
    }
    else {
      auto p1 = tgfx::Point(1+i,(float)height() -2 - frameHeight);
      auto p2 = tgfx::Point(1+i,(float)height() -2 );
      drawLine(canvas,p1,p2,color);
    }
    i++;
    idx += group;
  }
}

void FramesView::createAppHost() {
  appHost = std::make_unique<AppHost>();
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

void FramesView::setViewToLastFrames() {
  const int total = worker->GetFrameCount( *frames );

  viewData->zvStart = worker->GetFrameBegin( *frames, std::max( 0, total - 4 ) );
  if( total == 1 ) {
    viewData->zvEnd = worker->GetLastTime();
  }
  else {
    viewData->zvEnd = worker->GetFrameBegin( *frames, total - 1 );
  }
  if( viewData->zvEnd == viewData->zvStart ) {
    viewData->zvEnd = worker->GetLastTime();
  }
}

QSGNode* FramesView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  auto node = static_cast<QSGImageNode*>(oldNode);
  if(!tgfxWindow) {
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
  const auto dpos = tgfx::Point(0, 0);

  const uint64_t frameTarget = 1000 * 1000 * 1000 / viewData->frameTarget;

  auto p1 =
      dpos +
      tgfx::Point(0, round((float)height() - (float)height() * frameTarget * 2 / MaxFrameTime));
  auto p2 = dpos + tgfx::Point(width(), round((float)height() -
                                              (float)height() * frameTarget * 2 / MaxFrameTime));
  drawLine(canvas, p1, p2, 0x442222DD);

  p1 = dpos + tgfx::Point(0, round((float)height() - (float)height() * frameTarget / MaxFrameTime));
  p2 = dpos +
       tgfx::Point(width(), round((float)height() - (float)height() * frameTarget / MaxFrameTime));
  drawLine(canvas, p1, p2, 0x4422DDDD);

  p1 = dpos +
       tgfx::Point(0, round((float)height() - (float)height() * frameTarget / 2 / MaxFrameTime));
  p2 = dpos + tgfx::Point(width(), round((float)height() -
                                         (float)height() * frameTarget / 2 / MaxFrameTime));
  drawLine(canvas, p1, p2, 0x4422DD22);
}

void FramesView::drawSelection(tgfx::Canvas* canvas) {
  if(selectedStartFrame < 0 || selectedEndFrame < 0 ) return;

  const int frameWidth = GetFrameWidth(viewData->frameScale);
  const int group = GetFrameGroup(viewData->frameScale);
  const int totalFrames = static_cast<int>(worker->GetFrameCount(*frames));

  int minFrame = std::min(selectedStartFrame,selectedEndFrame);
  int maxFrame = std::max(selectedStartFrame,selectedEndFrame);
  float startX = frameToPixel(minFrame);
  float rectHeight = this->height();
  float rectWidth ;

  if(isLeftDagging) {
    int currentFrame = pixelToFrame(lastLeftDragPos.x());
    currentFrame = std::clamp(currentFrame,0,totalFrames - 1);

    if(currentFrame < dragStartFrame) {
      minFrame = currentFrame;
      maxFrame = dragStartFrame;
    }else {
      minFrame = dragStartFrame;
      maxFrame = currentFrame;
    }
  }

  if(minFrame == maxFrame) {
    rectWidth = frameWidth / group;
  }else {
    int frameCount = (maxFrame - minFrame) / group + 1 ;
    rectWidth = frameCount * (frameWidth / group);
  }

  auto rect = tgfx::Rect::MakeXYWH(startX,0.0,rectWidth,rectHeight);
  tgfx::Paint paint;
  paint.setAntiAlias(true);

  paint.setStyle(tgfx::PaintStyle::Fill);
  paint.setColor(tgfx::Color(0.0f,0.47f,0.84f,0.3f));
  canvas->drawRect(rect,paint);

  paint.setStyle(tgfx::PaintStyle::Stroke);
  paint.setColor(tgfx::Color(0.0f,0.47f,0.84f,1.0f));
  paint.setStrokeWidth(1.0f);
  canvas->drawRect(rect,paint);

  }

void FramesView::wheelEvent(QWheelEvent* event) {
  auto mode = 1;
  auto wheel = event->angleDelta().y();
  if (wheel > 0) {
    viewData->frameScale += mode;
  }
  else if (wheel < 0) {
    viewData->frameScale -= mode;
  }

  update();
  event->accept();
}

void FramesView::mousePressEvent(QMouseEvent *event) {

    if(event->button() == Qt::LeftButton) {
      isLeftDagging = true;
      lastLeftDragPos = event ->pos();

      int frame = pixelToFrame(event->pos().x());
      //frame = std::max(rangeFrameStart,std::min(frame,rangeFrameEnd));

      if(frame >= 0 && frame < worker->GetFrameCount(*frames)) {
        dragStartFrame = frame;
        selectedStartFrame = frame;
        selectedEndFrame = frame;

        if(m_timelineView) {
          int64_t startTime = worker->GetFrameBegin(*frames,frame);
          int64_t endTime = worker->GetFrameEnd(*frames,frame);
          m_timelineView->setTimeSelection(startTime,endTime);
          m_timelineView ->zoomToTimeRange(startTime ,endTime);
        }
      }
      event->accept();
      return;
  }

  if(event -> button() == Qt::RightButton) {
    isRightDragging = true;
    lastRightDragPos = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QQuickItem::mousePressEvent(event);
}

void FramesView::mouseMoveEvent(QMouseEvent* event) {
  if(isLeftDagging && (event->buttons()& Qt::LeftButton)) {
    lastLeftDragPos = event->pos();
    int currentFrame = pixelToFrame(event->pos().x());
    const int total = worker->GetFrameCount(*frames);

    if(currentFrame >= 0 && currentFrame < total) {
      if(currentFrame >= dragStartFrame) {
        selectedStartFrame = dragStartFrame;
        selectedEndFrame = currentFrame;
      }else {
        selectedStartFrame = currentFrame;
        selectedEndFrame = dragStartFrame;
      }

      if(m_timelineView) {
        int64_t startTime = worker->GetFrameBegin(*frames,selectedStartFrame);
        int64_t endTime = worker->GetFrameEnd(*frames,selectedEndFrame);
        if(startTime < endTime) {
          m_timelineView->setTimeSelection(startTime,endTime);
          m_timelineView ->zoomToTimeRange(startTime,endTime );
        }

      }
      update();
    }
    event->accept();
    return;
  }

  if(isRightDragging) {
    QPoint currentPos = event->pos();
    int deltaX = currentPos.x() - lastRightDragPos.x();
    int newViewOffset = viewOffset + deltaX;
    int leftBoundary = 2;

    if(newViewOffset > leftBoundary) {
      viewOffset = leftBoundary;
    }else {
      viewOffset = newViewOffset;
    }

    lastRightDragPos = currentPos;
    update();
    event->accept();
    return;
  }
QQuickItem::mouseMoveEvent(event);
}

void FramesView::mouseReleaseEvent(QMouseEvent* event) {
  if(event->button() == Qt::RightButton) {
    isRightDragging = false;
    setCursor(Qt::ArrowCursor);
    event->accept();
    return;
  }

  if(event->button() == Qt::LeftButton) {
    isLeftDagging = false;
    int frame = pixelToFrame(event->pos().x());
    if(frame == dragStartFrame) {
      selectedStartFrame = frame;
      selectedEndFrame = frame;
      if(m_timelineView) {
        int64_t startTime = worker->GetFrameBegin(*frames,frame);
        int64_t endTime = worker->GetFrameEnd(*frames,frame);
        m_timelineView->setTimeSelection(startTime,endTime);
        m_timelineView->zoomToTimeRange(startTime,endTime);
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
  const auto mouseX = event->pos().x();
  const int adjustedX = mouseX - viewOffset;
  const auto offset = adjustedX * group / frameWidth;
  const int total = worker->GetFrameCount(*frames);
  const int sel = viewData->frameStart + offset;

  if (sel >= 0 && sel < total) {
    QString text;
    if (group > 1) {
      auto frameTime = worker->GetFrameTime(*frames, sel);
      auto g = std::min(group, total - sel);
      for (int j = 1; j < g; ++j) {
        frameTime = std::max(frameTime, worker->GetFrameTime(*frames, sel + j));
      }
      text = QString("Frames:%1 - %2(%3)\nMax Frame Time:%4(%5FPS)\n")
                 .arg(sel)
                 .arg(sel + g - 1)
                 .arg(g)
                 .arg(tracy::TimeToString(frameTime))
                 .arg(1000000000.0 / frameTime);
    } else {
      const auto frameNumber = getFrameNumber(*frames, sel);
      frameHover = frameNumber;
      if (frames->name == 0) {
        const auto frameTime = worker->GetFrameTime(*frames, sel);
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
    text += QString("Time from start of program:%1")
                .arg(tracy::TimeToStringExact(worker->GetFrameBegin(*frames, sel)));

    QPoint globalPos = QCursor::pos();
    QToolTip::showText(globalPos, text, nullptr);

  } else {
    QToolTip::hideText();
  }
  QQuickItem::hoverMoveEvent(event);
}

void FramesView::updateTimeRange(int64_t start, int64_t end) {
  if (!frames || !worker) return;

  rangeTimestart = start;
  rangeTimeEnd = end;

  int newStartFrame = findFramesfromTime(start);
  int newEndFrame = findFramesfromTime(end);

  if(selectedStartFrame >= 0 && selectedEndFrame >= 0) {
    selectedStartFrame = newStartFrame;
    selectedEndFrame = newEndFrame;

    if(m_timelineView) {
      int64_t startTime = worker->GetFrameBegin(*frames,selectedStartFrame);
      int64_t endTime = worker->GetFrameEnd(*frames,selectedEndFrame);
      m_timelineView->setTimeSelection(startTime,endTime);
    }

  }

  rangeFrameStart = newStartFrame;
  rangeFrameEnd = newEndFrame;

}

