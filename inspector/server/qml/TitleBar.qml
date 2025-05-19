import QtQuick 2.6
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

KDDW.TitleBarBase {
    id: root
    color:  "#434343"
    heightWhenVisible: 30

    visible: titleBarCpp && titleBarCpp.visible

    Text {
        color: "white"
        font.bold: isFocused
        text: root.title
        anchors {
            left: parent.left
            leftMargin: 10
            verticalCenter: root.verticalCenter
        }
    }

    Row {
        id: buttonRow
        spacing: 10

        height: root.height - 10
        anchors {
            right: root.right
            rightMargin: 10
            verticalCenter: root.verticalCenter
        }

        Rectangle {
            id: closeButton
            color: "transparent"
            radius: 3
            border.width: closeButtonMouseArea.containsMouse ? 1 : 0
            border.color: "black"

            height: 17
            width: height

            Image {
                source: "qrc:/img/close.png"
                anchors.centerIn: parent

                MouseArea {
                    id: closeButtonMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.closeButtonClicked();
                    }
                }
            }
        }
    }
}
