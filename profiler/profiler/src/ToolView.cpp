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

#include "ToolView.h"
#include <MainView.h>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <TracyFileRead.hpp>
#include <iostream>

ToolView::ToolView(QWidget* parent) : QWidget(parent) {
  // setWindowFlags(Qt::FramelessWindowHint);
  // setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
  setStyleSheet("background-color: grey;");
  initView();
  initConnect();
}

ToolView::~ToolView() {

}

void ToolView::paintEvent(QPaintEvent* event) {
  resize(300, 200);
  return QWidget::paintEvent(event);
}

void ToolView::initView() {
  auto layout = new QVBoxLayout(this);
  auto lable = new QLabel("TGFX Profiler v1.0.0", this);
  QFont font;
  font.setFamily("Arial");
  font.setPointSize(21);
  font.setBold(true);
  lable->setFont(font);
  lable->setStyleSheet("Color: white");
  lable->setAlignment(Qt::AlignCenter);
  // lable->setStyleSheet("background-color: green;");

  textCombobox = new QComboBox;
  textCombobox->setEditable(true);

  auto buttonLayout = new QHBoxLayout;
  connectButton = new QPushButton("connect");
  openFileButton = new QPushButton("open file");

  buttonLayout->addWidget(connectButton);
  buttonLayout->addWidget(openFileButton);

  QFrame* line = new QFrame;
  line->setLineWidth(2);
  layout->addWidget(lable);
  // layout->addWidget(line);
  layout->addWidget(textCombobox);
  layout->addLayout(buttonLayout);
}

void ToolView::connect() {
  auto mainView = static_cast<MainView*>(this->parent());
  mainView->openConnectView();
}

void ToolView::openFile() {
  auto mainView = static_cast<MainView*>(this->parent());
  mainView->openFile();
}


void ToolView::initConnect() {
  QObject::connect(connectButton, &QPushButton::clicked, this, &ToolView::connect);
  QObject::connect(openFileButton, &QPushButton::clicked, this, &ToolView::openFile);
}
