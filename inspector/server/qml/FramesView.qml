import QtQuick 2.5
import QtQuick.Window 2.13
import FramesDrawer 1.0

Item {
    id: wind
    visible: true

    property var worker: framesViewLoader ? framesViewLoader.loaderWorker : null
    property var viewData: framesViewLoader ? framesViewLoader.loaderViewData : null

    Row {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: frameInfoArea
            width: 180
            height: parent.height
            color: "#535353"

            Column {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 1
                anchors.topMargin: 1
                spacing: 10

                Row {
                    spacing: 10
                    Text {
                        text: "Frame:"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                        width: 80
                    }
                    Text {
                        id: frameInfo
                        /* get the data from model */
                        text: /*model.getFirstFrame()*/ "0"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                    }
                }

                Row {
                    spacing: 10
                    Text {
                        text: "Time:"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                        width: 80
                    }
                    Text {
                        id: timeInfo
                        text: "0 ms"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                    }
                }

                Row {
                    spacing: 10
                    Text {
                        text: "Draw Calls:"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                        width: 80
                    }
                    Text {
                        id: drawCallsInfo
                        text: "0"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                    }
                }
            }
        }

        FramesDrawer {
            id: frameDrawer
            worker: wind.worker
            viewData: wind.viewData
            width: parent.width - frameInfoArea.width
            height: parent.height
            objectName: "framesDrawer"
        }
    }
}
