import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.impl

Item {
    width: 800
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: "#383838"
        z: -1

        Column {
            id: columnLayout
            anchors.fill: parent
            spacing: 2
            clip: true

            property var columnWidth: [width * 0.5, width * 0.3, width * 0.2]

            Rectangle {
                id: treeViewFilter
                width: parent.width
                height: 40
                color: "#535353"

                Row {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 2

                    ComboBox {
                        id: searchSwitch
                        clip: true
                        width: parent.width * 0.3
                        height: 30
                        model: ["Name", "Type"]

                        background: Rectangle {
                            color: "#383838"
                        }

                        contentItem: Text {
                            id: searchSwitchText
                            leftPadding: 10
                            text: searchSwitch.displayText
                            color: "#DDDDDD"
                            verticalAlignment: Text.AlignVCenter
                        }

                        indicator: Item {
                            id: indicatorItem
                            x: searchSwitch.width - indicatorImage.width - 10
                            y: (searchSwitch.availableHeight - height) / 2
                            rotation: 90
                            ColorImage {
                                id: indicatorImage
                                anchors.centerIn: parent
                                source: "qrc:/icons/arrow_icon.png"
                                color: "#DDDDDD"
                            }
                        }

                        delegate: ItemDelegate {
                            width: searchSwitch.width
                            height: 30
                            padding: 1

                            contentItem: Text {
                                text: modelData
                                color: "#DDDDDD"
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 10
                            }

                            background: Rectangle {
                                color: "#535353"
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
                                interactive: false
                                implicitHeight: contentHeight
                                model: searchSwitch.popup.visible ? searchSwitch.delegateModel : null
                            }
                        }
                    }

                    Item {
                        width: parent.width * 0.7
                        height: 30

                        TextField {
                            id: searchFiled
                            visible: searchSwitch.currentText == "Name"
                            color: "#DDDDDD"
                            anchors.fill: parent
                            placeholderText: "Enter name or regax..."
                            placeholderTextColor: "#888888"
                            background: Rectangle {
                                color: "#383838"
                                border.color: "#333333"
                                border.width: 1
                            }
                            onTextChanged: {
                                taskFilterModel.setTextFilter(text)
                            }
                        }

                        Item {
                            id: taskFilterSelector
                            anchors.fill: parent
                            visible: searchSwitch.currentText == "Type"

                            property bool isOpen: false

                            Rectangle {
                                id: taskTypeDisplay
                                anchors.fill: parent
                                color: "#383838"
                                border.color: "#333333"
                                border.width: 1
                                clip: true

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 6
                                    Layout.alignment: Qt.AlignVCenter

                                    Text {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        verticalAlignment: Text.AlignVCenter
                                        text: "Select types..."
                                        color: "#888888"
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        id: taskTypeDisplayIndicatorItem
                                        rotation: 90
                                        width: 20
                                        ColorImage {
                                            id: taskTypeDisplayIndicator
                                            anchors.centerIn: parent
                                            source: "qrc:/icons/arrow_icon.png"
                                            color: "#DDDDDD"

                                            MouseArea {
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: {
                                                    taskFilterSelector.isOpen = !taskFilterSelector.isOpen
                                                    if (taskFilterSelector.isOpen) {
                                                        typePopup.open()
                                                    } else {
                                                        typePopup.close()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                Popup {
                                    id: typePopup
                                    y: taskTypeDisplay.height
                                    width: taskTypeDisplay.width
                                    height: taskFilterTreeView.contentHeight
                                    padding: 2
                                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                                    background: Rectangle {
                                        color: "#383838"
                                    }

                                    contentItem: TreeView {
                                        id: taskFilterTreeView
                                        anchors.margins: 10
                                        model: taskFilterModel
                                        selectionModel: ItemSelectionModel {}
                                        rowSpacing: 1
                                        clip: true

                                        Component.onCompleted: {
                                            expandRecursively()
                                        }

                                        delegate: TreeViewDelegate {
                                            id: taskFilterTreeDelegate
                                            anchors.margins: 1
                                            readonly property real _padding: 1
                                            readonly property real szHeight: 40
                                            required property string name
                                            required property int filterType

                                            implicitWidth: taskFilterTreeView.width
                                            implicitHeight: szHeight

                                            indicator: Item {
                                                id: taskFilterIndicator
                                                x: 60 + taskFilterTreeDelegate.depth * taskFilterTreeDelegate.indentation
                                                implicitWidth: 20
                                                implicitHeight: taskFilterTreeDelegate.szHeight
                                                visible: taskFilterTreeDelegate.isTreeNode && taskFilterTreeDelegate.hasChildren
                                                rotation: taskFilterTreeDelegate.expanded ? 90 : 0
                                                TapHandler {
                                                    onSingleTapped: {
                                                        let index = taskFilterTreeDelegate.treeView.index(taskFilterTreeDelegate.model.row, taskFilterTreeDelegate.model.column)
                                                        taskFilterTreeDelegate.treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
                                                        taskFilterTreeDelegate.treeView.toggleExpanded(taskFilterTreeDelegate.model.row)
                                                    }
                                                }
                                                ColorImage {
                                                    anchors.centerIn: parent
                                                    source: "qrc:/icons/arrow_icon.png"
                                                    color: "#DDDDDD"
                                                }
                                            }

                                            contentItem: Rectangle {
                                                id: taskFilterItem
                                                anchors.fill: parent
                                                implicitHeight: taskFilterTreeDelegate.szHeight
                                                color: "#383838"

                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 2

                                                    CheckBox {
                                                        id: taskFilterCheckBox
                                                        checked: taskFilterModel.filterType & taskFilterTreeDelegate.filterType
                                                        background: Rectangle {
                                                            color: "#535353"
                                                        }
                                                        onClicked: {
                                                            let index = taskFilterTreeDelegate.treeView.index(taskFilterTreeDelegate.model.row, taskFilterTreeDelegate.model.column)
                                                            taskFilterModel.checkedItem(index, checked)
                                                        }
                                                        indicator: Rectangle {
                                                            implicitWidth: 28
                                                            implicitHeight: 28
                                                            x: taskFilterCheckBox.leftPadding + 2
                                                            y: taskFilterCheckBox.height / 2 - height / 2
                                                            color: "#383838"
                                                            Text {
                                                                x: 4
                                                                y: 0
                                                                text: "✓"
                                                                font.pointSize: 22
                                                                color: "#DDDDDD"
                                                                visible: taskFilterCheckBox.checked
                                                            }
                                                        }
                                                    }

                                                    Rectangle {
                                                        Layout.fillWidth: true
                                                        Layout.fillHeight: true
                                                        color: "#535353"

                                                        Row {
                                                            anchors.fill: parent
                                                            height: taskFilterTreeDelegate.szHeight
                                                            spacing: 0

                                                            Rectangle {
                                                                width: taskFilterIndicator.width + taskFilterTreeDelegate.depth * taskFilterTreeDelegate.indentation + 20
                                                                height: taskFilterIndicator.height
                                                                color: "transparent"
                                                            }

                                                            Text {
                                                                height: parent.height
                                                                horizontalAlignment: Text.AlignLeft
                                                                verticalAlignment: Text.AlignVCenter
                                                                color: "#DDDDDD"
                                                                clip: true
                                                                text: taskFilterTreeDelegate.name
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* treeView header *///
            Rectangle {
                id: treeViewHeader
                width: parent.width
                height: 40
                clip: true
                color: "#383838"

                Row {
                    anchors.fill: parent
                    spacing: 1
                    Rectangle {
                        width: columnLayout.columnWidth[0]
                        height: parent.height
                        color: "#535353"

                        Text {
                            anchors.centerIn: parent
                            text: "Task"
                            color: "#DDDDDD"
                            font.bold: true
                            clip: true
                        }
                    }

                    Rectangle {
                        width: columnLayout.columnWidth[1]
                        height: parent.height
                        color: "#535353"
                        Text {
                            anchors.centerIn: parent
                            text: "Self"
                            color: "#DDDDDD"
                            font.bold: true
                            clip: true
                        }
                    }

                    Rectangle {
                        width: columnLayout.columnWidth[2]
                        height: parent.height
                        color: "#535353"
                        Text {
                            anchors.centerIn: parent
                            text: "Weight"
                            color: "#DDDDDD"
                            font.bold: true
                            clip: true
                        }
                    }
                }
            }
            ///*main treeview*///
            Rectangle {
                id: treeView
                width: parent.width
                height: taskTreeView.contentHeight
                TreeView {
                    id: taskTreeView
                    width: parent.width
                    height: parent.height
                    anchors.margins: 10
                    clip: true
                    model: taskTreeModel
                    selectionModel: ItemSelectionModel {}
                    columnSpacing: 1
                    rowSpacing: 1

                    Component.onCompleted: {
                        expandRecursively()
                    }

                    delegate: TreeViewDelegate {
                        id: viewDelegate
                        anchors.margins: 1

                        readonly property real _padding: 1
                        readonly property real szHeight: 40
                        required property string name
                        required property string costTime
                        required property string weight

                        implicitWidth: columnLayout.columnWidth[model.column]
                        implicitHeight: szHeight

                        contentItem: Text {
                            color: "#DDDDDD"
                            clip: true
                            enabled: false
                            horizontalAlignment: {
                                switch (viewDelegate.model.column) {
                                    case 0:
                                        return Text.AlignLeft
                                    case 1:
                                    case 2:
                                        return Text.AlignHCenter
                                    default:
                                        return Text.AlignLeft
                                }
                            }
                            text: {
                                switch (viewDelegate.model.column) {
                                    case 0:
                                        return viewDelegate.name
                                    case 1:
                                        return viewDelegate.costTime
                                    case 2:
                                        return viewDelegate.weight
                                    default:
                                        return ""
                                }
                            }
                        }

                        background: Rectangle {
                            id: taskTreeBackground
                            anchors.fill: parent
                            color: {
                                if (viewDelegate.model.row === viewDelegate.treeView.currentRow) {
                                    return "#6B6B6B"
                                }
                                return "#535353"
                            }
                        }

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
                                anchors.centerIn: parent
                                source: "qrc:/icons/arrow_icon.png"
                                color: "#DDDDDD"
                            }
                        }
                    }
                }
                color: "#383838"
            }

            Rectangle {
                width: parent.width
                height: parent.height
                color: "#535353"
            }
        }
    }
}
