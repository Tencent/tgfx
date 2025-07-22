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
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtquick/ViewFactory.h>
#include <QQmlContext>
#include "AttributeModel.h"
#include "FramesDrawer.h"
#include "TaskTreeModel.h"
#include "TextureDrawer.h"
#include "TextureListDrawer.h"
#include "kddockwidgets/qtquick/views/Group.h"
namespace inspector {

class CustomViewFactory : public KDDockWidgets::QtQuick::ViewFactory {
 public:
  ~CustomViewFactory() override = default;

  QUrl tabbarFilename() const override {
    return QUrl("qrc:/qml/TabBar.qml");
  }

  QUrl separatorFilename() const override {
    return QUrl("qrc:/qml/Separator2.qml");
  }

  QUrl titleBarFilename() const override {
    return QUrl("qrc:/qml/TitleBar.qml");
  }

  QUrl groupFilename() const override {
    return QUrl("qrc:/qml/MyGroup.qml");
  }
};

InspectorView::InspectorView(std::string filePath, int width, QObject* parent)
    : QObject(parent), width(width), isOpenFile(true), worker(filePath) {
  initView();
  failedCreateWorker();
}

InspectorView::InspectorView(ClientData* clientData, int width, QObject* parent)
    : QObject(parent), width(width), worker(clientData->address.c_str(), clientData->port),
      clientData(clientData) {
  this->clientData->setConnected(true);
  initView();
}

InspectorView::~InspectorView() {
  if (clientData) {
    clientData->setConnected(false);
  }
}

void InspectorView::initView() {
  KDDockWidgets::Config::self().setViewFactory(new CustomViewFactory);
  qmlRegisterType<FramesDrawer>("FramesDrawer", 1, 0, "FramesDrawer");
  qmlRegisterType<TaskTreeModel>("TaskTreeModel", 1, 0, "TaskTreeModel");
  qmlRegisterType<AttributeModel>("AttributeModel", 1, 0, "AttributeModel");
  qmlRegisterType<TextureDrawer>("TextureDrawer", 1, 0, "TextureDrawer");
  qmlRegisterType<TextureListDrawer>("TextureListDrawer", 1, 0, "TextureListDrawer");
  qmlRegisterUncreatableType<KDDockWidgets::QtQuick::Group>(
      "com.kdab.dockwidgets", 2, 0, "GroupView", QStringLiteral("Internal usage only"));
  taskTreeModel = std::make_unique<TaskTreeModel>(&worker, &viewData, this);
  selectFrameModel = std::make_unique<SelectFrameModel>(&worker, &viewData, this);
  attributeModel = std::make_unique<AttributeModel>(&worker, &viewData, this);
  taskFilterModel = std::make_unique<TaskFilterModel>(&viewData, this);
  ispEngine = std::make_unique<QQmlApplicationEngine>(this);
  ispEngine->rootContext()->setContextProperty("workerPtr", &worker);
  ispEngine->rootContext()->setContextProperty("viewDataPtr", &viewData);
  ispEngine->rootContext()->setContextProperty("inspectorViewModel", this);
  ispEngine->rootContext()->setContextProperty("taskTreeModel", taskTreeModel.get());
  ispEngine->rootContext()->setContextProperty("taskFilterModel", taskFilterModel.get());
  ispEngine->rootContext()->setContextProperty("selectFrameModel", selectFrameModel.get());
  ispEngine->rootContext()->setContextProperty("attributeModel", attributeModel.get());
  KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(ispEngine.get());
  ispEngine->load(QUrl("qrc:/qml/InspectorView.qml"));
  if (ispEngine->rootObjects().isEmpty()) {
    qWarning() << "Failed to load InspectorView.qml";
    return;
  }
  auto ispWindow = qobject_cast<QWindow*>(ispEngine->rootObjects().first());
  if (ispWindow) {
    ispWindow->setTitle("Inspector");
    ispWindow->show();
    connect(ispWindow, &QWindow::visibilityChanged, [this](QWindow::Visibility visibility) {
      if (visibility == QWindow::Visibility::Hidden) {
        emit viewHide();
      }
    });
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
  connect(frameDrawer, &FramesDrawer::selectFrame, taskTreeModel.get(),
          &TaskTreeModel::refreshData);
  connect(frameDrawer, &FramesDrawer::selectFrame, selectFrameModel.get(),
          &SelectFrameModel::refreshData);
  connect(inspectorWindow, &QQuickWindow::closing, this, &InspectorView::onCloseView,
          Qt::QueuedConnection);
  connect(taskFilterModel.get(), &TaskFilterModel::filterTypeChange, taskTreeModel.get(),
          &TaskTreeModel::refreshData);
  connect(taskTreeModel.get(), &TaskTreeModel::selectTaskOp, attributeModel.get(),
          &AttributeModel::refreshData);
  connect(this, &InspectorView::closeView, dynamic_cast<StartView*>(parent()),
          &StartView::onCloseView);
}

void InspectorView::openStartView() {
  auto startView = dynamic_cast<StartView*>(parent());
  startView->showStartView();
}

bool InspectorView::saveFile() {
  if (saveFilePath.empty()) {
    return false;
  }
  return worker.saveFile(saveFilePath);
}

bool InspectorView::saveFileAs(const QUrl& filePath) {
  saveFilePath = filePath.path().toStdString();
  return worker.saveFile(saveFilePath);
}

void InspectorView::onCloseView(QQuickCloseEvent*) {
  Q_EMIT closeView(this);
}

void InspectorView::failedCreateWorker() {
  if (worker.hasExpection()) {
    QString errorMessage = "Inspector create failed, because: \n";
    for (auto& message : worker.getErrorMessage()) {
      errorMessage += message.c_str();
      errorMessage += "\n";
    }
    Q_EMIT failedOpenInspectorView(errorMessage);
  }
}

bool InspectorView::getIsOpenFile() const {
  return isOpenFile;
}

bool InspectorView::getHasSaveFilePath() const {
  return !saveFilePath.empty();
}

void InspectorView::nextFrame() {
  if (viewData.selectFrame + 1 > worker.getFrameCount() - 1) {
    return;
  }
  viewData.selectFrame++;
  auto inspectorWindow = qobject_cast<QQuickWindow*>(ispEngine->rootObjects().first());
  if (!inspectorWindow) {
    return;
  }
  auto frameDrawer = inspectorWindow->findChild<FramesDrawer*>("framesDrawer");
  Q_EMIT frameDrawer->selectFrame();
}

void InspectorView::preFrame() {
  if (viewData.selectFrame - 1 == uint32_t(-1)) {
    return;
  }
  viewData.selectFrame--;
  auto inspectorWindow = qobject_cast<QQuickWindow*>(ispEngine->rootObjects().first());
  if (!inspectorWindow) {
    return;
  }
  auto frameDrawer = inspectorWindow->findChild<FramesDrawer*>("framesDrawer");
  Q_EMIT frameDrawer->selectFrame();
}
}  // namespace inspector
