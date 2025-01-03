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

#include "ProfilerWindow.h"
#include <qguiapplication.h>
#include <qscreen.h>
#include <QToolBar>
#include <QPainter>
#include <QVBoxLayout>
#include "MainView.h"

ProfilerWindow::ProfilerWindow(QMainWindow* parent): QMainWindow(parent) {
  initWindow();
}

void ProfilerWindow::initToolBar() {
  quitAction = new QAction(QIcon(":/icons/exit.png"), tr("&quit"), this);
  saveFileAction = new QAction(tr("&save"), this);
  stopAction = new QAction(QIcon(":/icons/player_stop.png"),tr("&stop"), this);
  discardAction = new QAction(QIcon(":/icons/exit.png"),tr("&dicard"), this);

  topBar = new QToolBar(tr("Tools"));
  topBar->setMovable(false);
  topBar->setStyleSheet("background-color: blue");
  topBar->addAction(quitAction);
  topBar->addAction(saveFileAction);
  topBar->addAction(stopAction);
  topBar->addAction(discardAction);
}


void ProfilerWindow::setToolBarEnable() {

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