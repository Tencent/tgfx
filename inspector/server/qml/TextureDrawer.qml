import QtQuick 2.5
import QtQuick.Window 2.13
import FramesDrawer 1.0

Item {
    id: wind
    visible: true

    ///* use to create a module which named TextureDrawer *///
    FramesDrawer {
        id: frameView
        worker: workerPtr
        viewData: viewDataPtr
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        objectName: "framesView"
    }
}
