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
        }

        model: _layerAttributeModel

        Component.onCompleted:{

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

            //implicitWidth: padding + label.x + label.implicitWidth + padding
            implicitWidth: wind.width * 0.5
            implicitHeight: label.implicitHeight * 1.5

            readonly property real indent: 20
            readonly property real padding: 50

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
                id: background
                anchors.fill: parent
                color: (row % 2) === 0 ? "#202029" : "#2d2d36"
            }

            Text {
                id: indicator
                visible: root.isTreeNode && treeView.model.isExpandable(treeView.index(row, column))
                x: padding + (root.depth * root.indent)
                y: label.implicitHeight * 0.2
                text: root.expanded ? "▼" : "▶"
                color: "white"
                font.pixelSize: 15
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if(root.hasChildren){
                            if(root.expanded){
                                treeView.model.collapseRow(row);
                            }else{
                                treeView.model.expandRow(row);
                            }
                            treeView.toggleExpanded(row)
                        }else{
                            treeView.model.expandSubAttribute(treeView.index(row, column), row)
                        }
                    }
                }
            }

            Text {
                id: label
                x: padding + (root.isTreeNode ? (root.depth + 1) * root.indent : 0)
                y: label.implicitHeight * 0.2
                width: root.width - root.padding - x
                clip: false
                text: model.display
                color: "white"
                font.pixelSize: 15
            }
        }
    }
}