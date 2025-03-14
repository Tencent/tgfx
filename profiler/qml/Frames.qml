import QtQuick 2.5
import QtQuick.Window 2.13
import Frames 1.0

Window {
    id: wind
    visible: true
    width: _width
    height: 500
    flags: Qt.FramelessWindowHint
    color: "transparent"

    FramesView {
        id: frameView
        worker: _worker
        viewData: _viewData
        viewMode: _viewMode
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        objectName: "framesView"
    }
}
