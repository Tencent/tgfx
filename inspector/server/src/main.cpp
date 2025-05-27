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

#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtquick/ViewFactory.h>
#include <kddockwidgets/qtquick/views/DockWidget.h>
#include <qwidget.h>
#include "StartView.h"
#include <QQuickStyle>

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

int main(int argc, char* argv[]) {
  QApplication::setApplicationName("Inspector");
  QApplication::setOrganizationName("org.tgfx");
  QSurfaceFormat defaultFormat = QSurfaceFormat();
  defaultFormat.setRenderableType(QSurfaceFormat::RenderableType::OpenGL);
  defaultFormat.setVersion(3, 2);
  defaultFormat.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(defaultFormat);
  qputenv("QT_LOGGING_RULES", "qt.qpa.*=false");
  QQuickStyle::setStyle("Basic");

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

#else
  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

  QApplication app(argc, argv);
  KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);

  static bool initialized = false;
  if (!initialized) {
    initFrontend(KDDockWidgets::FrontendType::QtQuick);
    initialized = true;
  }

  auto& config = KDDockWidgets::Config::self();
  config.setSeparatorThickness(2);
  auto flags = config.flags() | KDDockWidgets::Config::Flag_TitleBarIsFocusable |
               KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible;

  config.setFlags(flags);
  config.setViewFactory(new CustomViewFactory());

  // 创建并显示StartView
  inspector::StartView* startView = new inspector::StartView();
  startView->showStartView();

  return app.exec();
}
