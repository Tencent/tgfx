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

#include "MainView.h"

class ProfilerWindow : public QMainWindow {
  Q_OBJECT
 public:
  ProfilerWindow(QMainWindow* parent = nullptr);
  void initWindow();
  void initConnect();
  void changeViewMode();
  void reversalPlayAction();
  void changePlayAction(bool pause);
  Q_SLOT void pushPlayAction();
  Q_SLOT void updateToolBar(ProfilerStatus status);

 protected:
  void initToolBar();

 private:
  bool pause = false;

  MainView* mainView;
  QToolBar* topBar;
  QAction* quitAction;
  QAction* saveFileAction;
  QAction* playAction;
  QAction* discardAction;
  QAction* statisticsAction;
};