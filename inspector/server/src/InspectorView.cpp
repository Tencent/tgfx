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

#include "InspectorView.h"
#include <kddockwidgets/qtquick/Platform.h>
#include <QQmlContext>
#include "AtttributeModel.h"
#include "TaskTreeModel.h"

namespace inspector {

InspectorView::InspectorView(std::string filePath, int width, QObject* parent)
    : QObject(parent), width(width), worker(filePath) {
  frames = worker.GetFrameData();
}

InspectorView::~InspectorView() {
  cleanView();
}

void InspectorView::initView() {
  cleanView();

  // qmlRegisterType<FramesView>("Frames", 1, 0, "FramesView");
  qmlRegisterType<TaskTreeModel>("TaskTreeModel", 1, 0, "TaskTreeModel");
  qmlRegisterType<AtttributeModel>("AtttributeModel", 1, 0, "AtttributeModel");

  ispEngine = new QQmlApplicationEngine(this);
  ispEngine->rootContext()->setContextProperty("workerPtr", (unsigned long long)&worker);
  ispEngine->rootContext()->setContextProperty("inspectorViewModel", this);

  KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(ispEngine);
  ispEngine->load((QUrl("qrc:/qml/InspectorView.qml")));

  if (ispEngine->rootObjects().isEmpty()) {
    qWarning() << "Failed to load InspectorView.qml";
    return;
  }

  auto ispWindow = qobject_cast<QWindow*>(ispEngine->rootObjects().first());
  if (ispWindow) {
    ispWindow->setTitle("Inspector");
    ispWindow->show();
  }
}

void InspectorView::cleanView() {
  if (ispWindow) {
    ispWindow->close();
    ispWindow = nullptr;
  }

  if (ispEngine) {
    ispEngine->deleteLater();
    ispEngine = nullptr;
  }
}

void InspectorView::openStartView() {
  cleanView();
  StartView* startView = new StartView(this);
  startView->showStartView();
}

void InspectorView::openTaskView() {
}
}  // namespace inspector
