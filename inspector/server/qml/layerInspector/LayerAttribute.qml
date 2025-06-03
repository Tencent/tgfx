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
                implicitWidth: column === 0 ? wind.width * 0.6 : wind.width * 0.4
                implicitHeight: isImage? wind.width * 0.3 : 40

                property  bool isImage: treeView.model.isImage(treeView.index(row, column))
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

                Component.onCompleted:{
                    _layerAttributeModel.flushImageChild.connect((objID) => {
                        if(objID === _layerAttributeModel.imageID(treeView.index(row, column))){
                            image.visible = Qt.binding(function () {
                                    return _layerAttributeModel.eyeButtonState(_layerAttributeModel.parent(treeView.index(row, column)))
                            })
                        }
                    });

                    imageProvider.imageFlush.connect((id) => {
                        if(id === _layerAttributeModel.imageID(treeView.index(row, column))) {
                            let number = _layerAttributeModel.imageID(treeView.index(row, column)).toString()
                            image.source = "image://RenderableImage/" + number + "-" + Date.now();
                        }
                    });
                }

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
                    anchors.verticalCenter : parent.verticalCenter
                    text: root.expanded ? "▼" : "▶"
                    color: "white"
                    font.pointSize: 22
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (root.hasChildren) {
                                if(treeView.model.rowCount(treeView.index(row, column)) === 1 && treeView.model.isRenderable(treeView.index(row, column))){
                                    treeView.model.expandSubAttribute(treeView.index(row, column), row)
                                }else{
                                    if (root.expanded) {
                                        treeView.model.collapseRow(row);
                                    } else {
                                        treeView.model.expandRow(row);
                                    }
                                    treeView.toggleExpanded(row)
                                }
                            } else {
                                treeView.model.expandSubAttribute(treeView.index(row, column), row)
                            }
                        }
                    }
                }

                Text {
                    id: label
                    visible: !treeView.model.isImage(treeView.index(row, column)) || root.isTreeNode
                    x: root.padding + (root.isTreeNode ? (root.depth + 1) * root.indent : 0)
                    //x: root.padding + (root.depth + 1) * root.indent
                    anchors.verticalCenter : parent.verticalCenter
                    //width: x + label.implicitWidth
                    clip: false
                    text: model.display
                    color: "white"
                    font.pointSize: 22
                }

                Rectangle{
                    id: imageArea
                    visible: treeView.model.isImage(treeView.index(row, column)) && !root.isTreeNode
                    color: "#383838"
                    x: root.padding + (root.isTreeNode ? (root.depth + 1) * root.indent : 0)
                    anchors.verticalCenter : parent.verticalCenter
                    width: root.implicitWidth - root.padding * 2
                    height: root.implicitHeight - root.padding * 2
                    Image{
                        id: image
                        visible: _layerAttributeModel.eyeButtonState(_layerAttributeModel.parent(treeView.index(row, column)))
                        anchors.centerIn : parent
                        width: parent.width * 0.6
                        height: width
                        cache: false
                        source: "image://RenderableImage/" + _layerAttributeModel.imageID(treeView.index(row, column)).toString()
                        fillMode: Image.PreserveAspectFit
                    }
                }

                Rectangle{
                    id: renderableSwitch
                    color: eyeIcon.checked ? "#808080" : "transparent";
                    width: 25
                    height: width
                    visible: !root.isTreeNode && treeView.model.isRenderable(treeView.index(row, column))
                    x: label.x + label.width + root.padding
                    anchors.verticalCenter : parent.verticalCenter
                    Image{
                        id: eyeIcon
                        anchors.fill: parent
                        source: "qrc:/icons/layerInspector/eye.svg"
                        property bool checked : treeView.model.eyeButtonState(treeView.index(row, column))
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                //eyeIcon.checked = !eyeIcon.checked;
                                treeView.model.setEyeButtonState(!eyeIcon.checked, treeView.index(row, column));
                                //treeView.model.DisplayImage(eyeIcon.checked, treeView.index(row, column));
                                eyeIcon.checked = Qt.binding(function (){
                                    return treeView.model.eyeButtonState(treeView.index(row, column))
                                })
                            }
                        }
                    }
                }
            }
        }
    }
}