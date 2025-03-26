import QtQuick
import QtQuick.Controls

Window {
    id: wind
    visible: true
    flags: Qt.FramelessWindowHint

    color: "#2d2d36"
    TreeView {
        id: treeView
        anchors.fill: parent
        anchors.margins: 0
        clip: true

        selectionModel: ItemSelectionModel {
            model: _layerTreeModel
        }


        model: _layerTreeModel

        property  var hoverRow : null

        // 触发选中的函数
        function selectItemByIndex(targetIndex) {

            expandRecursively(rootIndex);
            // 设置选中状态
            treeView.selectionModel.setCurrentIndex(targetIndex, ItemSelectionModel.Select)

            // 兼容旧版本的特殊处理
            treeView.__listView.currentIndex = targetIndex.row
        }

        Component.onCompleted:{
            _layerTreeModel.resetModel.connect(() => {
                expandRecursively(rootIndex);
            });
            _layerTreeModel.selectIndex.connect((index) => {
                selectItemByIndex(index);
            });
        }


        delegate: Item {
            id: root

            //implicitWidth: padding + label.x + label.implicitWidth + padding
            implicitWidth: wind.width * 0.5
            implicitHeight: label.implicitHeight * 1.5

            readonly property real indent: 20
            readonly property real padding: 50

            //标记为required的属性将由TreeView填充，与附加属性类似。
            //delegate间接通知TreeView它应该负责为它们分配值。可以将以下必需属性添加到delegate：
            //
            //指向包含delegate Item的TreeView
            required property TreeView treeView
            //项目是否代表树中的一个节点
            //视图中只有一列将用于绘制树，因此，只有该列中的delegate Item项才会将此属性设置为true。
            //树中的节点通常应根据其缩进，如果是则depth显示指示符。
            //其他列中的delegate Item将将此属性设置为，并将显示模型中其余列的数据（通常不缩进）。
            required property bool isTreeNode
            //绘制的model Item是否在视图中展开
            required property bool expanded
            //绘制的model Item是否在模型中有子项
            required property int hasChildren
            //包含delegate绘制的model Item的深度。model Item的深度与其在模型中的祖先数量相同
            required property int depth

            Rectangle {
                id: background
                anchors.fill: parent
                color: row === treeView.currentRow ? "#0092cb" : (row === treeView.hoverRow ? "black" : (row % 2 === 0 ? "#202029" : "#2d2d36"))
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
                    treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
                    treeView.model.MouseSelectedIndex(index);
                }
            }


            Text {
                id: indicator
                visible: root.isTreeNode && root.hasChildren
                x: padding + (root.depth * root.indent)
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
                font.pixelSize: 15
            }

            Text {
                id: label
                x: padding + (root.isTreeNode ? (root.depth + 1) * root.indent : 0)
                y: label.implicitHeight * 0.2
                width: root.width - root.padding - x
                clip: true
                text: model.display
                color: "white"
                font.pixelSize: 15
            }
        }
    }
}