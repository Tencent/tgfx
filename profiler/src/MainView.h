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

#include <QGraphicsView>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>
#include <thread>
#include "TracyWorker.hpp"
#include "Utility.h"
#include "View.h"
#include "layerInspector/LayerProfilerView.h"

class View;
class MainView : public QWidget {
  Q_OBJECT
 public:
  static std::thread loadThread;

  MainView(QWidget* parent = nullptr);
  ~MainView();

  void connectClient(const char* address, uint16_t port);
  void openFile();
  void openToolView();
  void openWebsocketServer();

  void openLayerProfilerWebsocketServer();

  void changeViewModeButton(bool pause);
  Q_SLOT void changeViewMode(bool pause);
  Q_SLOT void quitReadFile();
  Q_SLOT void saveFile();
  Q_SLOT void discardConnect();
  Q_SLOT void statView();
  Q_SIGNAL void statusChange(ProfilerStatus status);

 protected:
  void initToolView();
  void reopenToolView();

 private:
  QWidget* toolView;
  QWidget* connectView;
  View* centorView;
  LayerProfilerView* m_LayerProfilerView;
  QVBoxLayout* layout;
};