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

#pragma once
#include <QGraphicsView>
#include "TracyWorker.hpp"
#include "ViewData.h"

#define FRAME_VIEW_HEIGHT 50

constexpr uint64_t MaxFrameTime = 50 * 1000 * 1000;

class FramesView : public QGraphicsView {
public:
  FramesView(tracy::Worker& worker, int width, const tracy::FrameData* frames, QWidget* parent = nullptr);
  ~FramesView();

  void drawBackground(QPainter* painter, const QRectF& rect) override;
  void wheelEvent(QWheelEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

  uint64_t getFrameNumber(const tracy::FrameData& frameData, int i);
protected:
  void paintEvent(QPaintEvent* event) override;
  void initView();
  void scaleView(qreal scaleFactor);


private:
  tracy::Worker& worker;
  ViewData viewData;
  const tracy::FrameData* frames;
  int height;
  int width;
  int frameHover = -1;

  uint64_t frameTarget;
};