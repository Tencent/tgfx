import QtQuick
import QtQuick.Controls
import TaskTreeModel 1.0
import AtttributeModel 1.0

Item {
    width: 800
    height: column.height

    AtttributeModel {
        id: attributeModel
    }

    // Component.onCompleted: {
    //     taskTreeModel.setAttributeModel(attributeModel)
    //     //taskTreeModel.createTestData()
    //     typeTreeModel.createTestData()
    // }

    Rectangle {
        anchors.fill: parent
        color: "#535353"
        z: -1
    }

    TaskTreeModel {
        id: typeTreeModel
    }

    TaskTreeModel {
        id: taskTreeModel
    }

    Column {
        id: column
        width: parent.width

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

                    // onCurrentTextChanged: {
                    //     if(currentText === "Name") {
                    //         taskTreeModel.clearTypeFilter()
                    //     }
                    //     else {
                    //         taskTreeModel.clearTextFilter()
                    //     }
                    // }

                    background: Rectangle {
                        color: "#383838"
                        border.color: "#333333"
                        border.width: 1
                        radius: 2
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

                        background: Rectangle {
                            color: "#383838"
                            border.color: "#333333"
                            border.width: 1
                            radius: 2
                        }
                    }
                }

                ///* right text field to search for a name *///
                Item {
                    width: parent.width * 0.7
                    height: 30

                    TextField {
                        id: searchField
                        anchors.fill: parent
                        visible: searchSwitch.currentText === "Name"
                        placeholderText: "Enter name or regex..."
                        color: "#DDDDDD"
                        placeholderTextColor: "#aaaaaa"

                        background: Rectangle {
                            color: "#383838"
                            border.color: "#333333"
                            border.width: 1
                        }

                        onTextChanged: {
                            // taskTreeModel.setTextFilter(text)
                        }
                    }

                    ///* right combobox to select the type *///
                    Item {
                        id: taskFilterSelector
                        anchors.fill: parent
                        visible: searchSwitch.currentText === "Type"

                        function updateTaskTreeFilter() {
                            let selectedTypesList = Object.keys(selectedTypes).filter(
                                    key => selectedTypes[key]
                            )
                            if (selectedTypesList.length > 0) {
                                taskTreeModel.setTypeFilter(selectedTypesList);
                                console.log("过滤后模型行数:", taskTreeModel.rowCount())
                                taskTreeView.forceLayout()
                            } else {
                                taskTreeModel.clearTypeFilter();
                                console.log("清除过滤后模型行数:", taskTreeModel.rowCount())
                            }
                            // 强制刷新视图
                            taskTreeView.forceLayout()
                            console.log("视图刷新完成")
                        }

                        property var selectedTypes: ({})
                        property bool isOpen: false

                        Rectangle {
                            id: taskTypeDisplay
                            anchors.fill: parent
                            color: "#383838"
                            border.color: "#333333"
                            border.width: 1


                            Item {
                                anchors.fill: parent
                                anchors.margins: 5

                                Row {
                                    anchors.fill: parent
                                    spacing: 6

                                    Text {
                                        width: parent.width - 20
                                        height: parent.height
                                        verticalAlignment: Text.AlignVCenter
                                        text: Object.keys(taskFilterSelector.selectedTypes).length > 0
                                            ? Object.keys(taskFilterSelector.selectedTypes).join(", ")
                                            : "Select types..."
                                        color: "#DDDDDD"
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        height: parent.height
                                        verticalAlignment: Text.AlignVCenter
                                        text: taskFilterSelector.isOpen ? "▲" : "▼"
                                        color: "#DDDDDD"
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    taskFilterSelector.isOpen = !taskFilterSelector.isOpen
                                    if(taskFilterSelector.isOpen) {
                                        typePopup.open()
                                    } else {
                                        typePopup.close()
                                    }
                                }
                            }
                        }

                        Popup {
                            id: typePopup
                            y: taskTypeDisplay.height
                            width: taskTypeDisplay.width
                            height: Math.min(400, typeTreeView.contentHeight)
                            padding: 0
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                            background: Rectangle {
                                color: "#535353"
                            }

                            contentItem: TreeView {
                                id: typeTreeView
                                clip: true
                                model: typeTreeModel

                                delegate: Item {
                                    id: treeTypeItem
                                    required property bool isTreeNode
                                    required property bool expanded
                                    required property bool hasChildren
                                    required property int depth
                                    required property string name
                                    required property int row
                                    required property TreeView treeView

                                    implicitWidth: typeTreeView.width
                                    implicitHeight: 32

                                    Rectangle {
                                        anchors.fill: parent
                                        color: "transparent"

                                        MouseArea {
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onEntered: parent.color = "#636363"
                                            onExited: parent.color = "transparent"
                                            onClicked: {
                                                if(treeTypeItem.hasChildren) {
                                                    treeTypeItem.treeView.toggleExpanded(row)
                                                }
                                            }
                                        }

                                        Item {
                                            width: 28
                                            height: parent.height

                                            CheckBox {
                                                id: checkbox
                                                width: 20
                                                height: 20
                                                anchors {
                                                    left: parent.left
                                                    leftMargin: 6
                                                    right: parent.right
                                                    rightMargin: 4
                                                    top: parent.top
                                                    topMargin: 8
                                                    bottom: parent.bottom
                                                    bottomMargin: 4
                                                }
                                                checked: taskFilterSelector.selectedTypes[treeTypeItem.name] === true

                                                indicator: Rectangle {
                                                    width: 16
                                                    height: 16
                                                    color: "#383838"
                                                    radius: 2

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: checkbox.checked ? "✓" : ""
                                                        color: "#DDDDDD"
                                                        font.pixelSize: 14
                                                        visible: checkbox.checked
                                                    }
                                                }

                                                onCheckedChanged: {
                                                    taskFilterSelector.selectedTypes[treeTypeItem.name] = checked
                                                    taskFilterSelector.updateTaskTreeFilter();
                                                }
                                            }

                                            Rectangle {
                                                width: 1
                                                height: parent.height
                                                anchors.right: parent.right
                                                color: "#383838"
                                            }
                                        }

                                        Row {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 24
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 4

                                            Item { width: treeTypeItem.depth * 20; height: 1 }

                                            Item {
                                                width: 20
                                                height: 1
                                                visible: !treeTypeItem.hasChildren
                                            }

                                            Text {
                                                visible: treeTypeItem.hasChildren
                                                text: treeTypeItem.expanded ? "▼" : "▶"
                                                leftPadding: 8
                                                color: "#DDDDDD"
                                                width: 20
                                            }

                                            Text {
                                                text: treeTypeItem.name
                                                color: "#DDDDDD"
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Rectangle {
                                            width: parent.width
                                            height: 1
                                            anchors.bottom: parent.bottom
                                            color: "#383838"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ///* treeView header *///
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
            height: 600
            clip: true
            model: taskTreeModel

            property int selectedRow: -1

            delegate: Item {
                id: treeItem

                required property TreeView treeView
                required property bool isTreeNode
                required property bool expanded
                required property bool hasChildren
                required property int depth
                required property string name
                required property int startTime
                required property int endTime
                required property int duration
                required property int row
                required property int index

                implicitWidth: taskTreeView.width
                implicitHeight: 32

                Rectangle {
                    id: itemBackground
                    anchors.fill: parent
                    color: "transparent"

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (treeItem.hasChildren) {
                                treeItem.treeView.toggleExpanded(row)
                            }
                        }

                        hoverEnabled: true
                        onEntered: parent.color = "#6b6b6b"
                        onExited: parent.color = "transparent"
                    }

                    Row {
                        anchors.fill: parent

                        Rectangle {
                            width: parent.width * 0.3
                            height: parent.height
                            color: "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: treeItem.duration.toFixed(2) + "ms"
                                color: "#DDDDDD"
                                clip: true
                                width: parent.width
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
                                text: (treeItem.endTime - treeItem.startTime).toFixed(2) + "ms"
                                color: "#DDDDDD"
                                clip: true
                                width: parent.width
                            }
                            Rectangle {
                                width: 1
                                height: parent.height
                                anchors.right: parent.right
                                color: "#383838"
                            }
                        }

                        Item {
                            width: parent.width * 0.5
                            height: parent.height
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                spacing: 4
                                Item {
                                    width: treeItem.depth * 20
                                    height: 1
                                }

                                Item {
                                    width: 20
                                    height: 1
                                    visible: !treeItem.hasChildren
                                }

                                Text {
                                    id: indicator
                                    visible: treeItem.hasChildren
                                    text: treeItem.expanded ? "▼" : "▶"
                                    leftPadding: 5
                                    color: "#DDDDDD"
                                    width: 20
                                }

                                Text {
                                    text: treeItem.name
                                    color: "#DDDDDD"
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        anchors.bottom: parent.bottom
                        color: "#383838"
                    }
                }
            }
        }
    }

    function refreshData() {
        model.refresh()
    }
}
