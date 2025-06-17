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
#include "FramesDrawer.h"
#include "TaskTreeModel.h"
#include "kddockwidgets/qtquick/views/Group.h"

namespace inspector {

InspectorView::InspectorView(std::string filePath, int width, QObject* parent)
    : QObject(parent), width(width), worker(filePath) {
  initView();
  initConnect();
}

InspectorView::InspectorView(ClientData* clientData, int width, QObject* parent)
    : QObject(parent), width(width), clientData(clientData), worker(clientData->address.c_str(), clientData->port) {
  this->clientData->setConnected(true);
  initView();
  initConnect();
}

InspectorView::~InspectorView() {
  clientData->setConnected(false);
}

void InspectorView::initView() {
  qmlRegisterType<FramesDrawer>("FramesDrawer", 1, 0, "FramesDrawer");
  qmlRegisterType<TaskTreeModel>("TaskTreeModel", 1, 0, "TaskTreeModel");
  qmlRegisterType<AtttributeModel>("AtttributeModel", 1, 0, "AtttributeModel");
  qmlRegisterUncreatableType<KDDockWidgets::QtQuick::Group>("com.kdab.dockwidgets", 2, 0,
                                               "GroupView", QStringLiteral("Internal usage only"));
  taskTreeModel = std::make_unique<TaskTreeModel>(&worker, &viewData, this);
  selectFrameModel = std::make_unique<SelectFrameModel>(&worker, &viewData, this);
  taskFilterModel = std::make_unique<TaskFilterModel>(&viewData, this);
  ispEngine = std::make_unique<QQmlApplicationEngine>(this);
  ispEngine->rootContext()->setContextProperty("workerPtr", &worker);
  ispEngine->rootContext()->setContextProperty("viewDataPtr", &viewData);
  ispEngine->rootContext()->setContextProperty("inspectorViewModel", this);
  ispEngine->rootContext()->setContextProperty("taskTreeModel", taskTreeModel.get());
  ispEngine->rootContext()->setContextProperty("taskFilterModel", taskFilterModel.get());
  ispEngine->rootContext()->setContextProperty("selectFrameModel", selectFrameModel.get());
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
  initConnect();
}

void InspectorView::initConnect() {
  if (!ispEngine) {
    return;
  }
  auto inspectorWindow = qobject_cast<QQuickWindow*>(ispEngine->rootObjects().first());
  if (!inspectorWindow) {
    return;
  }
  auto frameDrawer = inspectorWindow->findChild<FramesDrawer*>("framesDrawer");
  connect(frameDrawer, &FramesDrawer::selectFrame, taskTreeModel.get(), &TaskTreeModel::refreshData);
  connect(frameDrawer, &FramesDrawer::selectFrame, selectFrameModel.get(), &SelectFrameModel::refreshData);
  connect(inspectorWindow, &QQuickWindow::closing, this, &InspectorView::onCloseView, Qt::QueuedConnection);
  connect(taskFilterModel.get(), &TaskFilterModel::filterTypeChange, taskTreeModel.get(), &TaskTreeModel::refreshData);
}

void InspectorView::openStartView() {
  auto startView = dynamic_cast<StartView*>(parent());
  startView->showStartView();
}

void InspectorView::onCloseView(QQuickCloseEvent*) {
  Q_EMIT closeView(this);
}
}  // namespace inspector
