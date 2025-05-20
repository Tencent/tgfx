#pragma once

#include <QMainWindow>
#include <QQmlEngine>
#include <QQmlApplicationEngine>
#include <kddockwidgets/qtquick/views/MainWindow.h>


#include "FramesView.h"
#include "StartView.h"
#include "Utility.h"
#include "TracyFileRead.hpp"
#include "TracyFileWrite.hpp"
#include "UserData.h"


namespace isp {
    class InspectorView : public QObject {
        Q_OBJECT

    public:
        InspectorView(tracy::FileRead& file, int width, const Config& config, QObject* parent = nullptr);
        ~InspectorView();

        void initView();
        void cleanView();
        Q_INVOKABLE void openStartView();
        Q_INVOKABLE void openTaskView();


    protected:

    private:
        int width;
        tracy::Worker worker;
        const tracy::FrameData* m_frames;

        ViewMode viewMode;
        ViewData viewData;
        UserData userData;
        const Config& config;
        bool connected = false;
        FramesView* framesView = nullptr;
        QQmlApplicationEngine* ispEngine = nullptr;
        QQmlApplicationEngine* framesEngine = nullptr;
        QQmlApplicationEngine* atttributeEngine = nullptr;
        QQmlApplicationEngine* meshEngine = nullptr;
        QQmlApplicationEngine* shaderEngine = nullptr;
        QQmlApplicationEngine* textureEngine = nullptr;
        QWindow* ispWindow = nullptr;




    };
}
