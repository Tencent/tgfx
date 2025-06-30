import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl

RowLayout {
    id: rootLayout
    signal clicked()
    property int indicatorRotation: indicatorItem.rotation
    property alias text: textItem.text

    anchors.fill: parent
    anchors.leftMargin: 10
    anchors.rightMargin: 10
    spacing: 10
    Layout.alignment: Qt.AlignVCenter

    Rectangle {
        id: indicatorItem
        width: 20
        rotation: rootLayout.indicatorRotation
        ColorImage {
            id: indicatorImage
            anchors.centerIn: parent
            source: "qrc:/icons/arrow_icon.png"
            color: "#DDDDDD"

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    rootLayout.clicked()
                }
            }
        }
    }

    Text {
        id: textItem
        Layout.fillWidth: true
        Layout.fillHeight: true
        verticalAlignment: Text.AlignVCenter
        color: "#DDDDDD"
        elide: Text.ElideRight
        font.bold: true
    }
}