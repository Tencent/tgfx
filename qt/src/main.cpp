/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QThread>
#include "TGFXView.h"
#include "qobject.h"

int main(int argc, char* argv[]) {

  QApplication::setApplicationName("Hello2D");
  QApplication::setOrganizationName("org.tgfx");
  QSurfaceFormat defaultFormat = QSurfaceFormat();
  defaultFormat.setRenderableType(QSurfaceFormat::RenderableType::OpenGL);
  defaultFormat.setVersion(3, 2);
  defaultFormat.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(defaultFormat);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#else
  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
  QApplication app(argc, argv);
  QApplication::setWindowIcon(QIcon(":/images/tgfx.png"));
  qmlRegisterType<hello2d::TGFXView>("TGFX", 1, 0, "TGFXView");
  QQmlApplicationEngine engine = {};
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  auto window = static_cast<QQuickWindow*>(engine.rootObjects().at(0));
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  window->setPersistentGraphics(true);
#else
  window->setPersistentOpenGLContext(true);
#endif
  window->setPersistentSceneGraph(true);
  return QApplication::exec();
}
