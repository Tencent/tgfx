import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.impl
import AttributeModel 1.0

Item {
    id: root
    width: 1120
    height: 800

    Rectangle {
        anchors.fill: parent
        color: "#535353"
        z: -1
    }

    SplitView {
        id: splitView
        anchors.fill: parent
        height: parent.height
        orientation: Qt.Vertical

        handle: Rectangle {
            implicitHeight: 2
            color: "#383838"
        }

        Column {
            id: summarySection
            SplitView.preferredHeight: 400

            property bool summaryExpanded: true

            Rectangle {
                id: summaryHeader
                width: parent.width
                height: 40
                color: "#535353"

                IndicatorText {
                    text: "Summary"
                    indicatorRotation: summarySection.summaryExpanded ? 90 : 0
                    onClicked: {
                        summarySection.summaryExpanded = !summarySection.summaryExpanded
                    }
                }
            }

            Rectangle {
                id: summaryListView
                width: parent.width
                height: summaryList.contentHeight
                color: "#535353"
                ListView {
                    id: summaryList
                    width: parent.width
                    height: parent.height
                    visible: summarySection.summaryExpanded
                    clip: true
                    model: attributeModel.summaryItems

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
                                text: modelData.name
                                color: "#dddddd"
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width * 0.3
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.value
                                color: "#dddddd"
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

        Column {
            id: processesSection
            visible: !attributeModel.isTask
            SplitView.preferredHeight: 400

            property bool processesExpanded: true

            Rectangle {
                id: processesHeader
                width: parent.width
                height: 40
                color: "#535353"

                IndicatorText {
                    text: "Processes"
                    indicatorRotation: processesSection.processesExpanded ? 90 : 0
                    onClicked: {
                        processesSection.processesExpanded = !processesSection.processesExpanded
                    }
                }
            }

            Rectangle {
                id: processesListView
                width: parent.width
                height: 200
                color: "#535353"
                // ListView {
                //     id: processesList
                //     width: parent.width
                //     height: parent.height
                //     visible: processesSection.processesExpanded
                //     clip: true
                //     model: attributeModel.
                //
                //     delegate: Rectangle {
                //         width: ListView.view.width
                //         height: 32
                //         color: "#535353"
                //
                //         Row {
                //             anchors.fill: parent
                //             anchors.leftMargin: 10
                //             anchors.rightMargin: 10
                //             spacing: 10
                //
                //             Text {
                //                 width: parent.width * 0.2
                //                 height: parent.height
                //                 leftPadding: 20
                //                 verticalAlignment: Text.AlignVCenter
                //                 text: model.key
                //                 color: "#dddddd"
                //                 font.pixelSize: 14
                //                 elide: Text.ElideRight
                //             }
                //
                //             Text {
                //                 width: parent.width * 0.3
                //                 height: parent.height
                //                 verticalAlignment: Text.AlignVCenter
                //                 text: model.value
                //                 color: "#dddddd"
                //                 font.pixelSize: 14
                //                 elide: Text.ElideRight
                //             }
                //         }
                //
                //         MouseArea {
                //             anchors.fill: parent
                //             hoverEnabled: true
                //             onEntered: parent.color = "#636363"
                //             onExited: parent.color = "#535353"
                //         }
                //     }
                //
                //     ScrollBar.vertical: ScrollBar {
                //         active: true
                //     }
                // }
            }
        }
    }
}