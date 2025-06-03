import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import TaskTreeModel 1.0
import AtttributeModel 1.0

Item {
    width: 800
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: "#535353"
        z: -1
    }

    Column {
        id: column
        anchors.fill: parent

        Rectangle {
            width: parent.width
            height: 40
            color: "#535353"

            Row {
                anchors.fill: parent
                anchors.margins: 5
                spacing: 2

                ///* left combobox to select the type *///
                ComboBox {
                    id: searchSwitch
                    width: parent.width * 0.3
                    height: 30
                    model: ["Name", "Type"]

                    onCurrentTextChanged: {
                        if(currentText === "Name") {
                        }
                        else {
                        }
                    }

                    background: Rectangle {
                        color: "#383838"
                    }

                    contentItem: Text {
                        leftPadding: 10
                        text: searchSwitch.displayText
                        color: "#DDDDDD"
                        verticalAlignment: Text.AlignVCenter
                    }

                    delegate: ItemDelegate {
                        width: searchSwitch.width
                        height: 30

                        contentItem: Text {
                            text: modelData
                            color: "#DDDDDD"
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }

                        background: Rectangle {
                            color: "#383838"
                        }
                    }

                    popup: Popup {
                        id: comboPopup
                        y: searchSwitch.height
                        width: searchSwitch.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1

                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: searchSwitch.popup.visible ? searchSwitch.delegateModel : null
                        }
                    }
                }

                ///* right text field to search for a name *///
            }
        }
        //
        /* treeView header *///
        Rectangle {
            width: parent.width
            height: 40
            color: "#535353"

            Rectangle {
                width: parent.width
                height: 2
                anchors.top: parent.top
                color: "#383838"
            }

            Rectangle {
                width: parent.width
                height: 2
                anchors.bottom: parent.bottom
                color: "#383838"
            }

            Row {
                anchors.fill: parent

                Rectangle {
                    width: parent.width * 0.3
                    height: parent.height
                    color: "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "Weight"
                        color: "#DDDDDD"
                        font.bold: true
                        clip: true
                    }
                    Rectangle {
                        width: 1
                        height: parent.height
                        anchors.right: parent.right
                        color: "#383838"
                    }
                }

                Rectangle {
                    width: parent.width * 0.2
                    height: parent.height
                    color: "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "Self"
                        color: "#DDDDDD"
                        font.bold: true
                        clip: true
                    }
                    Rectangle {
                        width: 1
                        height: parent.height
                        anchors.right: parent.right
                        color: "#383838"
                    }
                }

                Rectangle {
                    width: parent.width * 0.5
                    height: parent.height
                    color: "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "Task"
                        color: "#DDDDDD"
                        font.bold: true
                        clip: true
                    }
                }
            }
        }

        ///*main treeview*///
        TreeView {
            id: taskTreeView
            width: parent.width
            height: parent.height
            anchors.margins: 10
            clip: true
            model: taskTreeModel
            selectionModel: ItemSelectionModel {}

            delegate: TreeViewDelegate{
                id: viewDelegate

                readonly property real _padding: 5
                readonly property real szHeight: contentItem.implicitHeight * 2.5

                implicitWidth: _padding + contentItem.x + contentItem.implicitWidth + _padding
                implicitHeight: 40

                indicator: Item {
                    x: viewDelegate._padding + viewDelegate.depth * viewDelegate.indentation
                    implicitWidth: viewDelegate.szHeight
                    implicitHeight: viewDelegate.szHeight
                    visible: viewDelegate.isTreeNode && viewDelegate.hasChildren
                    rotation: viewDelegate.expanded ? 90 : 0
                    TapHandler {
                        onSingleTapped: {
                            let index = viewDelegate.treeView.index(viewDelegate.model.row, viewDelegate.model.column)
                            viewDelegate.treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
                            viewDelegate.treeView.toggleExpanded(viewDelegate.model.row)
                        }
                    }
                    ColorImage {
                        width: parent.width / 3
                        height: parent.height / 3
                        anchors.centerIn: parent
                        source: "qrc:/icons/arrow_icon.png"
                        color: palette.buttonText
                    }
                }
            }

        }
    }
}
