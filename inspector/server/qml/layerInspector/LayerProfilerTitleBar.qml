import QtQuick 2.6
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

KDDW.TitleBarBase {
    id: root
    color:  "#535353"
    heightWhenVisible: 40

    visible: titleBarCpp && titleBarCpp.visible

    Row{
        id:labelRow
        spacing: 10
        height: root.height - 15
        anchors {
            left: root.left
            leftMargin: 5
            verticalCenter: root.verticalCenter
        }

        Rectangle {
            id: labelImage
            color: "transparent"

            height: 25
            width: height

            Image {
                anchors.fill: parent
                source: root.title === "RenderTree" ? "qrc:/icons/layerInspector/tree.png" : "qrc:/icons/layerInspector/attribute.png"
            }
        }

        Text {
            color: "white"
            font.bold: isFocused
            text: root.title
            font.pointSize: 22
        }
    }

    Row {
        id: buttonRow
        spacing: 10

        height: root.height - 15
        anchors {
            right: root.right
            rightMargin: 10
            verticalCenter: root.verticalCenter
        }

        Rectangle {
            id: layerPick
            color: myImage.checked ? "#808080" : "transparent";

            height: 25
            width: height

            visible: root.title === "RenderTree"

            Image {
                id: myImage
                anchors.fill: parent
                source: "qrc:/icons/layerInspector/mouse.svg"
                property bool checked : false
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        myImage.checked = !myImage.checked;
                        _layerProfileView.SetHoveredSwitchState(myImage.checked);
                    }
                }
            }
        }

        Rectangle {
            id: flushTree
            color: "transparent"

            height: 25
            width: height

            Image {
                anchors.fill: parent
                source: "qrc:/icons/layerInspector/flush.svg"
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if(root.title === "RenderTree"){
                            _layerProfileView.flushLayerTree();
                        }else{
                            _layerProfileView.flushAttribute();
                        }
                    }
                }
            }
        }

        Rectangle {
            id: closeButton
            color: "transparent"
            radius: 3
            border.width: closeButtonMouseArea.containsMouse ? 1 : 0
            border.color: "black"

            height: 25
            width: height

            Image {
                source: "qrc:/icons/layerInspector/close.svg"
                anchors.fill: parent

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
