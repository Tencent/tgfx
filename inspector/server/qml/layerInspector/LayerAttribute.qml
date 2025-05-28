import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: wind
    visible: true
    color: "#535353"
    ColumnLayout {
        anchors.fill: parent
        Rectangle {
            color: "#383838"
            Layout.fillWidth: true
            height: 2
        }
        TreeView {
            id: treeView
            Layout.fillWidth : true
            Layout.fillHeight : true
            clip: true

            selectionModel: ItemSelectionModel {
            }

            model: _layerAttributeModel

            Component.onCompleted: {

                _layerAttributeModel.expandItemRow.connect((row) => {
                    expand(row);
                });

                _layerAttributeModel.collapseItemRow.connect((row) => {
                    collapse(row);
                });

                _layerAttributeModel.modelReset.connect(() => {
                    treeView.positionViewAtRow(0, TableView.Contain);
                });
            }

            delegate: Item {
                id: root

                implicitWidth: padding + label.x + label.implicitWidth + padding
                //implicitWidth: wind.width * 0.5
                implicitHeight: 40

                readonly property real indent: 20
                readonly property real padding: 10

                //指向包含delegate Item的TreeView
                required property TreeView treeView
                //项目是否代表树中的一个节点
                required property bool isTreeNode
                //绘制的model Item是否在视图中展开
                required property bool expanded
                //绘制的model Item是否在模型中有子项
                required property int hasChildren
                //包含delegate绘制的model Item的深度。model Item的深度与其在模型中的祖先数量相同
                required property int depth


                Rectangle {
                    id: background1
                    x: 0
                    y: 0
                    width: root.implicitWidth
                    height: root.implicitHeight - 1
                    color:  "#535353"
                }

                Rectangle {
                    id: background2
                    x: 0
                    y: background1.height
                    width: wind.width
                    height: 1
                    color: "#383838"
                }

                Text {
                    id: indicator
                    visible: root.isTreeNode && treeView.model.isExpandable(treeView.index(row, column))
                    x: root.padding + (root.depth * root.indent)
                    y: label.implicitHeight * 0.2
                    text: root.expanded ? "▼" : "▶"
                    color: "white"
                    font.pointSize: 22
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (root.hasChildren) {
                                if (root.expanded) {
                                    treeView.model.collapseRow(row);
                                } else {
                                    treeView.model.expandRow(row);
                                }
                                treeView.toggleExpanded(row)
                            } else {
                                treeView.model.expandSubAttribute(treeView.index(row, column), row)
                            }
                        }
                    }
                }

                Text {
                    id: label
                    //x: root.padding + (root.isTreeNode ? (root.depth + 1) * root.indent : 0)
                    x: root.padding + (root.depth + 1) * root.indent
                    y: label.implicitHeight * 0.2
                    width: root.width - root.padding - x
                    clip: false
                    text: model.display
                    color: "white"
                    font.pointSize: 22
                }
            }
        }
    }
}