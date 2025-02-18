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

#include "MainView.h"
#include <qevent.h>
#include <QQmlContext>
#include <QVBoxLayout>
#include "FramesView.h"
#include "ProfilerWindow.h"
#include "ToolView.h"
#include "TracyFileRead.hpp"
#include "View.h"
#include "src/profiler/TracyFileselector.hpp"

MainView::MainView(QWidget* parent): QWidget(parent) {
  setAttribute(Qt::WA_StyledBackground);
  setStyleSheet("background-color: black;");
  initToolView();
}

MainView::~MainView() {
}

void MainView::initToolView() {
  toolView = new ToolView(this);
  layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(toolView);
}

void MainView::reopenToolView() {
  Q_EMIT statusChange(ProfilerStatus::None);
  if (!toolView) {
    toolView = new ToolView(this);
  }
  layout->addWidget(toolView);
}

void MainView::saveFile(){
  if (centorView) {
    centorView->saveFile();
  }
}

void MainView::changeViewModeButton(bool pause) {
  auto profilerWindow = (ProfilerWindow*)parentWidget();
  profilerWindow->changePlayAction(pause);
}

void MainView::changeViewMode(bool pause) {
  if (centorView) {
    centorView->changeViewMode(pause);
  }
}

void MainView::quitReadFile(){
  if (centorView) {
    delete centorView;
    centorView = nullptr;
  }
  reopenToolView();
}

void MainView::discardConnect() {
  if (centorView) {
    delete centorView;
    centorView = nullptr;
  }
  reopenToolView();
}

void MainView::connectClient(const char* address, const uint16_t port) {
  toolView->setParent(nullptr);
  tracy::Config config;
  centorView = new View(address, port, this->width(), config, this);
  if (!centorView->isConnected()) {
    discardConnect();
    return;
  }
  layout->addWidget(centorView);
  Q_EMIT statusChange(ProfilerStatus::Connect);
}

void MainView::openFile() {
  tracy::Fileselector::OpenFile( "tracy", "Tracy Profiler trace file", [&]( const char* fn ) {
    auto file = std::shared_ptr<tracy::FileRead>(tracy::FileRead::Open(fn));
    if (file) {
      toolView->setParent(nullptr);

      tracy::Config config;
      centorView = new View(*file, this->width(), config, this);
      layout->addWidget(centorView);
      Q_EMIT statusChange(ProfilerStatus::ReadFile);
      delete toolView;
      toolView = nullptr;
    }
  });
}

void MainView::openWebsocketServer() {
  toolView->setParent(nullptr);
  tracy::Config config;
  centorView = new View(this->width(), config, this);
  if (!centorView->isConnected()) {
    discardConnect();
    return;
  }
  layout->addWidget(centorView);
  Q_EMIT statusChange(ProfilerStatus::Connect);
}

void MainView::openToolView() {
  toolView->setParent(this);
}
