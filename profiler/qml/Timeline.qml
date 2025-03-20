import QtQuick 2.5
import QtQuick.Window 2.13
import Timeline 1.0
import QtQuick.Controls 2.13

Window {
    id: wind
    visible: true
    width: 800
    height: 600
    flags: Qt.FramelessWindowHint
    color: "transparent"

    TimelineView {
        id: timelineView
        worker: workerPtr
        viewData: viewDataPtr
        viewMode: viewModePtr
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        objectName: "timelineView"
    }
}
