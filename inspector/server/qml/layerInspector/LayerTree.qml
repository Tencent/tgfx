import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: wind
    visible: true
    color : "#535353"
    ColumnLayout{
        anchors.fill: parent
        Rectangle{
            color: "#383838"
            Layout.fillWidth : true
            height: 2
        }
        TreeView {
            id: treeView
            Layout.fillWidth : true
            Layout.fillHeight : true
            clip: true
            rowSpacing: 1

            selectionModel: ItemSelectionModel {
                model: _layerTreeModel
            }


            model: _layerTreeModel

            property  var hoverRow : null

            // 触发选中的函数
            function selectItemByIndex(targetIndex) {

                expandRecursively(-1, -1);
                // 设置选中状态
                treeView.selectionModel.setCurrentIndex(targetIndex, ItemSelectionModel.ClearAndSelect);

                treeView.positionViewAtIndex(targetIndex, TableView.Contain);
            }

            Component.onCompleted:{
                _layerTreeModel.expandAllTree.connect(() => {
                    expandRecursively(-1, -1);
                });
                _layerTreeModel.selectIndex.connect((index) => {
                    selectItemByIndex(index);
                });
            }


            delegate: Item {
                id: root

                //implicitWidth: padding + label.x + label.implicitWidth + padding
                implicitWidth: column === 0 ? wind.width * 0.55 : wind.width * 0.45
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
                    color: row === treeView.currentRow ? "#6b6b6b" : (row === treeView.hoverRow ? "#c6c6c6" : "#535353")
                }

                Rectangle {
                    id: background2
                    x: 0
                    y: background1.height
                    width: root.implicitWidth
                    height: 1
                    color: "#383838"
                }

                MouseArea {
                    id: hoverArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: {
                        treeView.hoverRow = row
                        let index = treeView.index(row, column)
                        treeView.model.MouseHoveredIndex(index);
                    }
                    onClicked: {
                        let index = treeView.index(row, column)
                        if(!treeView.selectionModel.isSelected(index)){
                            treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect)
                            treeView.model.MouseSelectedIndex(index);
                        }
                    }
                }


                Text {
                    id: indicator
                    visible: root.isTreeNode && root.hasChildren
                    x: root.padding + (root.depth * root.indent)
                    y: label.implicitHeight * 0.2
                    text: root.expanded ? "▼" : "▶"
                    color: "white"
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            let index = treeView.index(row, column)
                            treeView.toggleExpanded(row)
                        }
                    }
                    font.pointSize: 22
                }

                Text {
                    id: label
                    x: root.padding + (root.isTreeNode ? (root.depth + 1) * root.indent : 0)
                    y: label.implicitHeight * 0.2
                    width: root.width - root.padding - x
                    clip: true
                    text: model.display
                    color: "white"
                    font.pointSize: 22
                }
            }
        }
    }
}