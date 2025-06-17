import QtQuick 2.5
import QtQuick.Window 2.13
import FramesDrawer 1.0

Item {
    id: frameView
    visible: true
    property int fontSize: 14
    property string fontColor: "#DDDDDD"

    Row {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: frameInfoArea
            width: 180
            height: parent.height
            color: "#383838"

            ListView {
                id: selectFrameDataPreview
                width: parent.width
                height: parent.height
                clip: true

                model: selectFrameModel

                delegate: Rectangle {
                    id: selectFrameViewDelegate
                    implicitHeight: 30
                    implicitWidth: selectFrameDataPreview.width
                    color: "#535353"
                    anchors.topMargin: 10
                    clip: true

                    required property string name
                    required property string value

                    Text {
                        x: 6
                        y: 6
                        font.pixelSize: frameView.fontSize
                        color: frameView.fontColor
                        text: selectFrameViewDelegate.name
                    }

                    Text {
                        x: (selectFrameDataPreview.width / 2) + 6
                        y: 6
                        font.pixelSize: frameView.fontSize
                        color: frameView.fontColor
                        text: selectFrameViewDelegate.value
                    }
                }
            }
        }

        FramesDrawer {
            id: frameDrawer
            worker: workerPtr
            viewData: viewDataPtr
            width: parent.width - frameInfoArea.width
            height: parent.height
            objectName: "framesDrawer"
        }
    }
}
