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
#include <QDialog>
#include <QQmlApplicationEngine>
#include <QSpinBox>
#include "FramesView.h"
#include "MainView.h"
#include "StatisticView.h"
#include "TimelineController.h"
#include "TimelineView.h"
#include "TracyFileRead.hpp"
#include "TracyFileWrite.hpp"
#include "TracyWorker.hpp"
#include "UserData.h"
#include "ViewData.h"
#include "socket/WebSocketServer.h"
#include "layerInspector/LayerProfilerView.h"

class SaveFileDialog : public QDialog {
  Q_OBJECT
 public:
  SaveFileDialog(std::string& filename, QWidget* parent = nullptr);
  void initWidget();
  void initConnect();
  QSpinBox* createSpinBox(int min, int max, int step, int defaultValue);
  void getValue(std::string& filename, int& compressionChosse, int& zstdLevel,
                int& compressionStreams);
  Q_SLOT void zstdLevelChanged();

 private:
  std::string filename;
  QLayout* compressionLayout;
  QSpinBox* zstdSpinBox;
  QSpinBox* streamSpinBox;
  QPushButton* confirmButton;
  QPushButton* cancelButton;
};

class View : public QWidget {
  Q_OBJECT
 public:
  View(int width, const Config& config, QWidget* parent = nullptr);
  View(const char* addr, uint16_t port, int width, const Config& config, QWidget* parent = nullptr);
  View(tracy::FileRead& file, int width, const Config& config, QWidget* parent = nullptr);

  ~View();

  ViewData& getViewData() {
    return viewData;
  }
  bool save();
  bool isConnected() const {
    return connected;
  }

  void changeViewMode(bool pause);
  void saveFile();
  void initView();
  void ViewImpl();
  Q_SLOT void changeViewModeButton(ViewMode mode);
  Q_SLOT void openStatisticsView();

  void timerEvent(QTimerEvent* event) override;

  const char* sourceSubstitution(const char* srcFile) const;
  bool validateSourceAge() const {
    return validateSrcAge;
  }

  Range m_statRange;

 private:
  bool connected = false;
  int width;
  LayerProfilerView* layerProfilerView;
  tracy::Worker worker;

  ViewMode viewMode;
  ViewData viewData;
  UserData userData;
  QQmlApplicationEngine* timelineEngine;
  QQmlApplicationEngine* framesEngine;
  std::thread saveThread;
  std::string filenameStaging;

  const Config& config;
  FramesView* framesView = nullptr;
  TimelineView* timelineView = nullptr;
  QMainWindow* statMainWindow = nullptr;
  StatisticsView* statisticsView = nullptr;
  SourceView* sourceView = nullptr;
  SaveFileDialog* saveFileDialog = nullptr;
  QDialog* connectDialog = nullptr;
  int timerId;

  //source file regex member...
  std::vector<SourceRegex> srcSubstitutions;
  bool srcRegexValid = true;
  bool validateSrcAge = true;

};