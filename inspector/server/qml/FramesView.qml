import QtQuick 2.5
import QtQuick.Window 2.13
import Frames 1.0

Item {
    id: wind
    visible: true

    FramesView {
        id: frameView
        worker: workerPtr
        viewData: viewDataPtr
        viewMode: viewModePtr
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        objectName: "framesView"
    }
}
