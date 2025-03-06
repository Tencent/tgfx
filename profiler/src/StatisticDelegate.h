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

#pragma once
#include <QStyledItemDelegate>
#include "StatisticModel.h"
#include "StatisticView.h"

class StatisticsDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  explicit StatisticsDelegate(StatisticsModel* model, View* v, QObject* parent = nullptr);
  ~StatisticsDelegate() override;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  void drawStatusIcon(QPainter* painter, const QRect& rect, const QModelIndex& index) const;

 private:
  QColor hoverColor;
  QColor textColor;
  QSize iconSize;
  View* view;
  StatisticsModel* model = nullptr;
};
