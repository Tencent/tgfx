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
#include <QComboBox>
#include <QDockWidget>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include "FramesView.h"
#include "SourceView.h"
#include "StatisticModel.h"
#include "ViewData.h"

class StatisticsModel;
class StatisticsView : public QWidget {
  Q_OBJECT

 public:
  explicit StatisticsView(tracy::Worker& workerRef, ViewData& viewDataRef, View* view,
                          FramesView* framesView, SourceView* srcView, QWidget* parent = nullptr);

  Q_SLOT void updateColumnSizes();
  Q_SLOT void updateZoneCountLabels();
  Q_SLOT void onLimitRangeToggled(bool active);
  Q_SLOT void onStatgeRangeChanged(int64_t start, int64_t end, bool active);

  void setupUI();
  void setupConnections();
  void setupTableView();

  //source view
  void viewSource(const char* fileName, int line);
  bool srcFileValid(const char* fn, uint64_t olderThan, const tracy::Worker& worker, View* view);
  void showContentMenu(const QPoint& pos);

  StatisticsModel* getModel() const {
    return model;
  }

 private:
  tracy::Worker& worker;
  ViewData& viewData;
  View* view;
  FramesView* framesView;
  QTableView* tableView;
  StatisticsModel* model;
  SourceView* srcView;
  QWidget* centralWidget;
  QComboBox* accumulationModeCombo;
  QLineEdit* filterEdit;
  QPushButton* clearFilterButton;
  QPushButton* limitRangeBtn;
  QLabel* mtotalZonesLabel;
  QLabel* mvisibleZonesLabel;
  const char* srcViewFile;
};
