import QtQuick 2.5
import QtQuick.Window 2.13
import Timeline 1.0

Window {
    id: wind
    visible: true
    width: 800
    height: 600

    TimelineView {
        id: timelineView
        worker: _worker
        viewData: _viewData
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        objectName: "timelineView"
    }
}
