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

#include <qevent.h>
#include <QToolTip>

#include "FramesView.h"
#include "TracyPrint.hpp"

FramesView::FramesView(tracy::Worker& worker, ViewData& viewData, int width, const tracy::FrameData* frames, QWidget* parent)
  : worker(worker)
  , viewData(viewData)
  , height(FRAME_VIEW_HEIGHT)
  , width(width)
  , frames(frames)
  , frameTarget(1000 * 1000 * 1000 / viewData.frameTarget)
  , QGraphicsView(parent){
  initView();
}

FramesView::~FramesView() {

}

void FramesView::initView() {
  QGraphicsScene *scene = new QGraphicsScene(this);
  scene->setItemIndexMethod(QGraphicsScene::NoIndex);
  scene->setSceneRect(0, 0, width, height);
  setScene(scene);
  setMouseTracking(true);
  setViewportUpdateMode(BoundingRectViewportUpdate);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setStyleSheet("background-color: rgb(77, 79, 73)");
}

void FramesView::scaleView(qreal scaleFactor) {
  qreal factor = transform().scale(scaleFactor, scaleFactor).mapRect(QRectF(0, 0, 1, 1)).width();
  if (factor < 0.07 || factor > 100)
    return;
  if (viewData.frameScale < 1 || scaleFactor < 1) {
    viewData.frameScale *= scaleFactor;
  }
  update();
}


uint64_t FramesView::getFrameNumber(const tracy::FrameData& frameData, int i) {
  if( frameData.name == 0 ) {
    const auto offset = worker.GetFrameOffset();
    if(offset == 0) {
      return i;
    }
    else {
      return i + offset - 1;
    }
  }
  return i + 1;
}

const char* GetFrameColor(uint64_t time, uint64_t target) {
  return time > target * 2 ? "#e22720" :
         time > target     ? "#e0e200" :
         time > target / 2 ? "#00e200" : "#0fa2e5";
}

const int GetFrameWidth(int frameScale) {
  return frameScale == 1 ? 6 : ( frameScale < 1 ? 8 : 1 );
}

const int GetFrameWidth(double frameScale) {
  return 8 * frameScale == 0 ? 1 : 8 * frameScale;
}

const int GetFrameGroup2(int frameWidth) {
  return frameWidth > 1 ? 1 : 4;
}

const int GetFrameGroup(int frameScale) {
    return frameScale < 2 ? 1 : ( 1 << ( frameScale - 1 ) );
};

void FramesView::paintEvent(QPaintEvent* event) {
  QGraphicsView::paintEvent(event);

  auto painter = QPainter(this->viewport());
  assert(worker.GetFrameCount(*frames) != 0);
  const int frameWidth = GetFrameWidth(viewData.frameScale);
  const int group = GetFrameGroup2(frameWidth);
  const int total = worker.GetFrameCount(*frames);
  const int onScreen = (width - 2) / frameWidth;

  int i = 0, idx = 0;
  while (i < onScreen && viewData.frameStart + idx < total) {
    auto frameTime = worker.GetFrameTime(*frames, viewData.frameStart + idx);
    const auto frameHeight = std::max(1.f, float( std::min<uint64_t>(MaxFrameTime, frameTime)) / MaxFrameTime * (height - 2));
    auto color = GetFrameColor(frameTime, frameTarget);
    if (frameWidth != 1) {
      painter.setPen(QColor(0, 0, 0, 0));
      painter.setBrush(QColor(color));
      QPoint p1 = QPoint(2 + i * frameWidth, height - 1 - frameHeight);
      QPoint p2 = QPoint(frameWidth + i * frameWidth, height - 1);
      painter.drawRect(QRect(p1, p2));
    }
    else {
      painter.setPen(QColor(color));
      QPoint p1 = QPoint(1 + i, height - 2 - frameHeight);
      QPoint p2 = QPoint(1 + i, height - 2);
      painter.drawLine(p1, p2);
    }
    i++;
    idx += group;
  }
}

void FramesView::drawBackground(QPainter* painter, const QRectF& rect) {
  const auto dpos = QPoint(0, 0);

  const uint64_t frameTarget = 1000 * 1000 * 1000 / viewData.frameTarget;
  QColor redLineColor(106, 70, 65);
  QColor yellowLineColor(106, 107, 65);
  QColor greenLineColor(67, 107, 65);

  painter->setPen(redLineColor);
  painter->drawLine(dpos + QPoint(0, round(height - height * frameTarget * 2 / MaxFrameTime)),
    dpos + QPoint(width, round(height - height * frameTarget * 2 / MaxFrameTime)));

  painter->setPen(yellowLineColor);
  painter->drawLine(dpos + QPoint(0, round(height - height * frameTarget    / MaxFrameTime)),
    dpos + QPoint(width, round(height - height * frameTarget     / MaxFrameTime)));

  painter->setPen(greenLineColor);
  painter->drawLine(dpos + QPoint(0, round(height - height * frameTarget / 2 / MaxFrameTime)),
    dpos + QPoint(width, round(height - height * frameTarget / 2 / MaxFrameTime)));
}

void FramesView::wheelEvent(QWheelEvent* event) {
  scaleView(pow(2., -event->angleDelta().y() / 240.0));
  event->accept();
}

void FramesView::mouseMoveEvent(QMouseEvent* event) {
  const int frameWidth = GetFrameWidth(viewData.frameScale);
  const int group = GetFrameGroup2(frameWidth);
  auto mouseX = event->pos().x();
  const auto offset = mouseX * group / frameWidth;
  const int total = worker.GetFrameCount( *frames );

  const int sel = viewData.frameStart + offset;
  QString text;
  if (sel < total) {
    if(group > 1) {
      auto frameTime = worker.GetFrameTime(*frames, sel);
      auto g = std::min(group, total - sel);
      for (int j = 1; j < g; ++j) {
        frameTime = std::max(frameTime, worker.GetFrameTime(*frames, sel + j));
      }
      text = QString("Frames: %1 - %2(%3)\nMax frame time:%4(%5 FPS)\n")
        .arg(sel)
        .arg(sel + g - 1)
        .arg(g)
        .arg(tracy::TimeToString(frameTime))
        .arg(1000000000.0 / frameTime);
    }
    else {
      const auto frameNumber = getFrameNumber(*frames, sel);
      frameHover = frameNumber;
      if (frames->name == 0) {
        const auto frameTime = worker.GetFrameTime(*frames, sel);
        const auto strFrameTime = tracy::TimeToString(frameTime);
        if (sel == 0) {
          text = QString("Tracy Initialization\nTime:%1\n").arg(strFrameTime);
        }
        else if (!worker.IsOnDemand()) {
          text = QString("Frame: %1\nFrame Time: %2 (%3 FPS)\n")
            .arg(frameNumber)
            .arg(strFrameTime)
            .arg(1000000000.0 / frameTime);
        }
        else if (sel == 1) {
          text = QString("Missed frames\nTime: %1")
            .arg(tracy::TimeToString(worker.GetFrameTime(*frames, 1)));
        }
        else {
          text = QString("Frame: %1\nFrame Time: %2 (%3 FPS)\n")
            .arg(frameNumber)
            .arg(strFrameTime)
            .arg(1000000000.0 / frameTime);
        }
      }
    }
    text += QString("Time fomr start of program: %1")
      .arg(tracy::TimeToStringExact(worker.GetFrameBegin(*frames, sel)));
  }
  QToolTip::showText(event->globalPosition().toPoint(), text, this);
  QGraphicsView::mouseMoveEvent(event);
}