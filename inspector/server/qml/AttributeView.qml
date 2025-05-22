import QtQuick
import QtQuick.Window
import QtQuick.Controls
import AtttributeModel 1.0

Item {
    id: root
    width: 1120

    height: 800

    AtttributeModel {
        id: attributeModel
        isOpSelected: root.isOpSelected
        Component.onCompleted: {
            console.log("Model loaded, row count:", rowCount())
        }
    }

    property bool isOpSelected: true

    Rectangle {
        anchors.fill: parent
        color: "#535353"
        z: -1
    }

    Column {
        id: mainColumn
        width: parent.width
        spacing: 1

        Rectangle {
            id: summarySection
            width: parent.width
            height: summaryHeader.height + (summarySection.summaryExpanded ? summaryList.height : 0)
            color: "#535353"

            property bool summaryExpanded: true

            Rectangle {
                id: summaryHeader
                width: parent.width
                height: 40
                color: "#535353"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    text: summarySection.summaryExpanded ? "▼" : "▶"
                    color: "white"
                    font.pixelSize: 15
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 30
                    text: "Summary"
                    color: "white"
                    font.pixelSize: 14
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        summarySection.summaryExpanded = !summarySection.summaryExpanded
                    }
                }
            }

            ListView {
                id: summaryList
                width: parent.width
                height: Math.min(contentHeight, 200)
                visible: summarySection.summaryExpanded
                clip: true
                anchors.top: summaryHeader.bottom
                model: attributeModel

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 32
                    color: "#535353"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Text {
                            width: parent.width * 0.2
                            height: parent.height
                            leftPadding: 20
                            verticalAlignment: Text.AlignVCenter
                            text: model.key
                            color: "white"
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width * 0.3
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: model.value
                            color: "white"
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                    }

                    // 鼠标悬停效果
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: parent.color = "#636363"
                        onExited: parent.color = "#535353"
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    active: true
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color:"#333333"
        }

        Rectangle {
            id: processesSection
            width: parent.width
            height: processesHeader.height + (processesSection.processesExpanded ? processesList.height : 0)
            color: "#535353"
            visible: root.isOpSelected

            property bool processesExpanded: true

            Rectangle {
                id: processesHeader
                width: parent.width
                height: 40
                color: "#535353"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    text: processesSection.processesExpanded ? "▼" : "▶"
                    color: "white"
                    font.pixelSize: 14
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 30
                    text: "Processes"
                    color: "white"
                    font.pixelSize: 14
                    font.bold: true
                }


                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        processesSection.processesExpanded = !processesSection.processesExpanded
                    }
                }
            }

            ListView {
                id: processesList
                width: parent.width
                height: Math.min(contentHeight, 300)
                visible: processesSection.processesExpanded
                clip: true
                anchors.top: processesHeader.bottom
                model: root.isOpSelected ? attributeModel : null

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 32
                    color: "#535353"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Text {
                            width: parent.width * 0.2
                            height: parent.height
                            leftPadding: 20
                            verticalAlignment: Text.AlignVCenter
                            text: model.key
                            color: "white"
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width * 0.3
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: model.value
                            color: "white"
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: parent.color = "#636363"
                        onExited: parent.color = "#535353"
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    active: true
                }
            }
        }
    }
}