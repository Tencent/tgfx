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

#include "View.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QQuickWindow>
#include <QQmlContext>
#include <QQmlApplicationEngine>
#include "FramesView.h"
#include "TimelineView.h"
#include "TracySysUtil.hpp"

View::View(const char* addr, uint16_t port, int width, const tracy::Config& config, QWidget* parent)
  : width(width)
  , worker(addr, port, config.memoryLimit == 0 ? -1 :
    ( config.memoryLimitPercent * tracy::GetPhysicalMemorySize() / 100 ) )
  , config(config)
  , QWidget(parent)
{
  initView();
}

View::View(tracy::FileRead& file, int width, const tracy::Config& config, QWidget* parent)
  : width(width)
  , worker(file)
  , userData(worker.GetCaptureProgram().c_str(), worker.GetCaptureTime())
  , config(config)
  , QWidget(parent) {
  initView();
  userData.StateShouldBePreserved();
  userData.LoadState(viewData);
}

View::~View() {
  userData.SaveState(viewData);
}

void View::initView() {
  ViewImpl();
}

void View::ViewImpl() {
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  qmlRegisterType<FramesView>("Frames", 1, 0, "FramesView");
  framesEngine = new QQmlApplicationEngine(QUrl(QStringLiteral("qrc:/qml/Frames.qml")));
  framesEngine->rootContext()->setContextProperty("_worker", (unsigned long long)&worker);
  framesEngine->rootContext()->setContextProperty("_viewData", (unsigned long long)&viewData);
  // framesEngine->rootContext()->setContextProperty("_width", width);
  auto quickWindow = static_cast<QQuickWindow*>(framesEngine->rootObjects().value(0));
  auto framesWidget = createWindowContainer(quickWindow);
  framesWidget->setFixedHeight(50);

  qmlRegisterType<tracy::Worker>("tracy", 1, 0, "TracyWorker");
  qmlRegisterType<TimelineView>("Timeline", 1, 0, "TimelineView");
  timelineEngine = new QQmlApplicationEngine(QUrl(QStringLiteral("qrc:/qml/Timeline.qml")));
  timelineEngine->rootContext()->setContextProperty("_worker", (unsigned long long)&worker);
  timelineEngine->rootContext()->setContextProperty("_viewData", (unsigned long long)&viewData);
  quickWindow = static_cast<QQuickWindow*>(timelineEngine->rootObjects().value(0));
  auto timelineWidget = createWindowContainer(quickWindow);
  timelineWidget->resize(1000, 1000);

  layout->addWidget(framesWidget);

  layout->addWidget(timelineWidget);
}
