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
#include <QVBoxLayout>
#include "FramesView.h"
#include "ToolView.h"
#include "TracyFileRead.hpp"
#include "View.h"
#include "src/profiler/TracyFileselector.hpp"

MainView::MainView(QWidget* parent): QWidget(parent) {
  initView();
}

MainView::~MainView() {
  if (layout) {
    delete layout;
  }
  if (toolView) {
    delete toolView;
  }
}

void MainView::initView() {
  toolView = new ToolView(this);
  layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(toolView);
}

void MainView::openConnectView() {

}

void MainView::openFile() {
  toolView->setParent(NULL);
  tracy::Fileselector::OpenFile( "tracy", "Tracy Profiler trace file", [&]( const char* fn ) {
    auto file = std::shared_ptr<tracy::FileRead>(tracy::FileRead::Open(fn));
    if (file) {
      tracy::Config config;
      centorView = new View(*file, this->width(), config, this);
      layout->addWidget(centorView);
      // loadThread = std::thread([file] {
      //
      // });
    }
  });
}

void MainView::openToolView() {
  toolView->setParent(this);
}
