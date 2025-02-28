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

#include "ProfilerWindow.h"
#include <qguiapplication.h>
#include <qscreen.h>
#include <QToolBar>
#include "view/MainView.h"

ProfilerWindow::ProfilerWindow(QMainWindow* parent) : QMainWindow(parent) {
  initWindow();
  initConnect();
  updateToolBar(ProfilerStatus::None);
}

void ProfilerWindow::initToolBar() {

  topBar = new QToolBar(tr("Tools"), this);
  topBar->setMovable(false);
  // topBar->setStyleSheet("background-color: blue");
  quitAction = new QAction(QIcon(":/icons/power.png"), tr("&quit"), this);
  topBar->addAction(quitAction);

  saveFileAction = new QAction(QIcon(":/icons/save.png"), tr("&save"), this);
  topBar->addAction(saveFileAction);

  playAction = new QAction(QIcon(":/icons/pause.png"), tr("&pause"), this);
  topBar->addAction(playAction);

  discardAction = new QAction(QIcon(":/icons/discard.png"), tr("&dicard"), this);
  topBar->addAction(discardAction);
}

void ProfilerWindow::updateToolBar(ProfilerStatus status) {
  if (status != ProfilerStatus::Connect) {
    changePlayAction(false);
  }

  quitAction->setEnabled(status == ProfilerStatus::ReadFile);
  saveFileAction->setEnabled(status == ProfilerStatus::Connect);
  playAction->setEnabled(status == ProfilerStatus::Connect);
  discardAction->setEnabled(status == ProfilerStatus::Connect);
}

void ProfilerWindow::changeViewMode() {
  mainView->changeViewMode(pause);
}

void ProfilerWindow::reversalPlayAction() {
  changePlayAction(!pause);
}

void ProfilerWindow::changePlayAction(bool pause) {
  if (!playAction->isEnabled()) {
    return;
  }
  if (pause) {
    playAction->setIcon(QIcon(":/icons/next.png"));
    playAction->setToolTip(tr("&start"));
  } else {
    playAction->setIcon(QIcon(":/icons/pause.png"));
    playAction->setToolTip(tr("&pause"));
  }
  this->pause = pause;
}

void ProfilerWindow::pushPlayAction() {
  reversalPlayAction();
  changeViewMode();
}

void ProfilerWindow::initWindow() {
  initToolBar();
  addToolBar(Qt::TopToolBarArea, topBar);
  mainView = new MainView;
  setCentralWidget(mainView);

  QScreen* screen = QGuiApplication::primaryScreen();
  QRect rect = screen->availableGeometry();
  resize(rect.size().width(), rect.height());
}

void ProfilerWindow::initConnect() {
  connect(mainView, &MainView::statusChange, this, &ProfilerWindow::updateToolBar);
  connect(saveFileAction, &QAction::triggered, mainView, &MainView::saveFile);
  connect(quitAction, &QAction::triggered, mainView, &MainView::quitReadFile);
  connect(discardAction, &QAction::triggered, mainView, &MainView::discardConnect);
  connect(playAction, &QAction::triggered, this, &ProfilerWindow::pushPlayAction);
}