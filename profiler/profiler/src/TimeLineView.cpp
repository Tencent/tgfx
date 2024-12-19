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

#include "TimelineView.h"

TimelineView::TimelineView(tracy::Worker& worker, QWidget* parent)
  :worker(worker), QGraphicsView(parent){

}

TimelineView::~TimelineView() {

}

void TimelineView::drawMouseLine() {

}

void TimelineView::drawTimeline() {

}

void TimelineView::paintEvent(QPaintEvent* event) {

  return QWidget::paintEvent(event);
}

void TimelineView::mouseMoveEvent(QMouseEvent* event) {
  return QWidget::mouseMoveEvent(event);
}