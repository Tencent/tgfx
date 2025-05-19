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

#include <QTreeView>
#include <QStandardItemModel>
#include <QHoverEvent>
#include <QDebug>

class HoverTreeView : public QTreeView {
  Q_OBJECT
public:
  HoverTreeView(QWidget* parent = nullptr) : QTreeView(parent), m_lastHoverIndex(QModelIndex()) {
    setMouseTracking(true);
  }

Q_SIGNALS:
    void hoverIndexChanged(const QModelIndex& index);

protected:
  void mouseMoveEvent(QMouseEvent* event) override {
    // 获取鼠标位置的 QModelIndex
    QModelIndex index = indexAt(event->pos());

    // 如果悬浮的 index 发生变化，发送信号
    if (index != m_lastHoverIndex) {
      m_lastHoverIndex = index;
      emit hoverIndexChanged(index);
    }

    QTreeView::mouseMoveEvent(event);
  }

private:
  QModelIndex m_lastHoverIndex; // 当前悬停的 QModelIndex
};

