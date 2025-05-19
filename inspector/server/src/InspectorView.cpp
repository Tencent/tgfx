//
///* only for loading qml and getting data *///
//

#include "InspectorView.h"
#include "TaskTreeModel.h"
#include "AtttributeModel.h"
#include <QQmlContext>
#include <QQmlApplicationEngine>
#include <QWidget>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/qtquick/views/DockWidget.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/ViewFactory.h>
#include <kddockwidgets/qtquick/views/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>

namespace isp {




    isp::InspectorView::InspectorView(tracy::FileRead& file, int width, const Config& config, QObject* parent) : QObject(parent), width(width), worker(file), viewMode(ViewMode::Paused),
      userData(worker.GetCaptureProgram().c_str(), worker.GetCaptureTime()), config(config) {
        m_frames = worker.GetFramesBase();
    }

    isp::InspectorView::~InspectorView() {
        cleanView();
    }

    void isp::InspectorView::initView() {
        cleanView();

        qmlRegisterType<ViewMode>("com.example", 1, 0, "ViewMode");
        qmlRegisterType<FramesView>("Frames", 1, 0, "FramesView");
        qmlRegisterType<isp::TaskTreeModel>("TaskTreeModel", 1, 0, "TaskTreeModel");
        qmlRegisterType<AtttributeModel>("AtttributeModel", 1, 0, "AtttributeModel");


        ispEngine = new QQmlApplicationEngine(this);
        ispEngine->rootContext()->setContextProperty("workerPtr", (unsigned long long)&worker);
        ispEngine->rootContext()->setContextProperty("viewDataPtr", &viewData);
        ispEngine->rootContext()->setContextProperty("viewModePtr", (unsigned long long)&viewMode);
        ispEngine->rootContext()->setContextProperty("inspectorViewModel", this);

        KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(ispEngine);
        ispEngine->load((QUrl("qrc:/qml/InspectorView.qml")));

        if(ispEngine->rootObjects().isEmpty()) {
            qWarning() << "Failed to load InspectorView.qml";
            return;
        }

        auto ispWindow = qobject_cast<QWindow*>(ispEngine->rootObjects().first());
        if(ispWindow) {
            ispWindow->setTitle("Inspector");
            ispWindow->show();
        }

    }

    void InspectorView::cleanView() {
        if(ispWindow) {
            ispWindow->close();
            ispWindow = nullptr;
        }

        if(ispEngine) {
            ispEngine->deleteLater();
            ispEngine = nullptr;
        }
    }

    void InspectorView::openStartView() {
        cleanView();
        isp::StartView* startView = new isp::StartView(this);
        startView->showStartView();
    }

    void InspectorView::openTaskView() {
    }
}
