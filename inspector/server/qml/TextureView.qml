import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import TextureDrawer 1.0
import TextureListDrawer 1.0
Item {
    id: root
    width: 800
    height: 600

    // 主布局
    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        // 左侧图片显示区域
        Rectangle {
            id: textureDisplayArea
            SplitView.minimumWidth: parent.width * 0.7
            SplitView.maximumWidth: parent.width - 50
            SplitView.preferredWidth: parent.width * 0.8  // 初始宽度设为父容器的80%
            color: "#535353"

            TextureDrawer {
                id: textureDrawer
                width:parent.width
                height:parent.height
                anchors.fill: parent
            }
        }
        handle: Rectangle {
            implicitWidth: 2
            implicitHeight: parent.height-2
            color: "#333333"
            opacity: 1

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                drag.target: parent
                drag.axis: Drag.XAxis
                drag.minimumX: 0
                drag.maximumX: root.width
            }
        }
        // 右侧标签页区域
        Rectangle {
            id: tabArea
            SplitView.fillWidth: true
            SplitView.minimumWidth: 5
            color: "#434343"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // 标签页标题栏
                TabBar {
                    id: tabBar
                    currentIndex: 1  // 默认选中Output标签
                    background: Rectangle { color: "#434343" }  // 深色背景

                    // Input标签
                    TabButton {
                        text: "Input"
                        width: implicitWidth + 20
                        height: parent.height
                        background: Rectangle {
                            color: tabBar.currentIndex === 0 ? "#535353" : "transparent"
                        }
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    // Output标签（带关闭按钮）
                    TabButton {

                        text: "Output"
                        width: implicitWidth + 20
                        height: parent.height
                        background: Rectangle {
                            color: tabBar.currentIndex === 1 ? "#535353" : "transparent"
                        }
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                // 标签页内容
                StackLayout {
                    id: stackLayout
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabBar.currentIndex


                    TextureListDrawer {
                        id: inputList
                    }

                    TextureListDrawer {
                        id: outputList
                    }
                }
            }
        }
    }
    Component.onCompleted: {
        inputList.selectedImage.connect(textureDrawer.onSelectedImage)
        outputList.selectedImage.connect(textureDrawer.onSelectedImage)
    }
}
