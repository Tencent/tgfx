import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import TextureDrawer 1.0
import TextureListDrawer 1.0

//#333333 分割线
//#434343 标签页背景
//#535353 标签页选中背景，纹理背景

Item {
    id: root
    width: 800
    height: 600
        Column {
            anchors.fill: parent
            spacing: 0
            //顶部分割线
            Rectangle {
                width: parent.width
                height: 2
                color: "#333333"
            }
            SplitView {
                width: parent.width
                height: parent.height-2
                orientation: Qt.Horizontal
                // 左侧图片显示区域
                Rectangle {
                    id: textureDisplayArea
                    SplitView.minimumWidth: parent.width * 0.7
                    SplitView.maximumWidth: parent.width - 50
                    SplitView.preferredWidth: parent.width * 0.8  // 初始宽度设为父容器的80%
                    color: "#282828"

                    TextureDrawer {
                        id: textureDrawer
                        width: parent.width
                        height: parent.height
                        anchors.fill: parent
                    }
                }
                handle: Rectangle {
                    implicitWidth: 2
                    implicitHeight: parent.height - 2
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
                    color: "#383838"

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        TabBar {
                            id: tabBar
                            width: parent.width
                            currentIndex: 1  // 默认选中Output标签
                            background: Rectangle {
                                color: "#383838"
                            }
                            spacing: 2

                            // Input标签
                            TabButton {
                                id: inputTabButton
                                text: "Input"
                                width: outputTabButton.width

                                background: Rectangle {
                                    color: tabBar.currentIndex === 0 ? "#535353" : "#434343"
                                    Rectangle {
                                        width: parent.width
                                        height: tabBar.currentIndex === 0 ? 0 : 2
                                        color: "#383838"
                                        anchors.bottom: parent.bottom
                                        visible: tabBar.currentIndex !== 0
                                    }
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            TabButton {
                                id: outputTabButton
                                text: "Output"
                                width: implicitWidth
                                background: Rectangle {
                                    color: tabBar.currentIndex === 1 ? "#535353" : "#434343"
                                    Rectangle {
                                        width: parent.width
                                        height: tabBar.currentIndex === 1 ? 0 : 2
                                        color: "#383838"
                                        anchors.bottom: parent.bottom
                                        visible: tabBar.currentIndex === 0
                                    }
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
                                imageLabel: "Input"
                            }

                            TextureListDrawer {
                                id: outputList
                                imageLabel: "Output"
                            }
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
