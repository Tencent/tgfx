import QtQuick 2.6
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW


KDDW.TabBarBase {
    id: root

    implicitHeight: 30
    currentTabIndex: 0

    Rectangle {
        id: tabBarBackground
        color: "#444444"
        anchors.fill: parent
        z: tabBarRow.z + 100
    }

    function getTabAtIndex(index) {
        return tabBarRow.children[index];
    }

    function getTabIndexAtPosition(globalPoint) {
        for (var i = 0; i < tabBarRow.children.length; ++i) {
            var tab = tabBarRow.children[i];
            var localPt = tab.mapFromGlobal(globalPoint.x, globalPoint.y);
            if (tab.contains(localPt)) {
                return i;
            }
        }

        return -1;
    }

    Row {
        id: tabBarRow
        z: root.mouseAreaZ.z + 1

        anchors.fill: parent
        spacing: 0

        property int hoveredIndex: -1

        Repeater {
            model: root.groupCpp ? root.groupCpp.tabBar.dockWidgetModel : 0
                Rectangle {
                    id: tab
                    height: parent.height
                    width: 150
                    readonly property bool isCurrent: index == root.groupCpp.currentIndex
                    color: (isCurrent ? "#535353" : "#434343")
                    border.color: "black"  // 非当前tab边框设为黑色
                    border.width: isCurrent ? 0 : 1  // 当前tab不显示边框

                    // 当前tab的特殊边框处理(只显示上、左、右边框)
                    Rectangle {
                        visible: isCurrent
                        anchors {
                            top: parent.top
                            left: parent.left
                            right: parent.right
                        }
                        height: 1  // 边框高度
                        color: "black"  // 当前tab的上边框设为黑色
                    }

                    readonly property int tabIndex: index

                Text {
                    anchors.centerIn: parent
                    text: title
                    color: "white"
                }


                Rectangle {
                    id: closeButton
                    color: "transparent"
                    radius: 3
                    border.width: closeButtonMouseArea.containsMouse ? 1 : 0
                    border.color: "black"

                    height: 17
                    width: height
                    anchors {
                        right: parent.right
                        rightMargin: 4
                        verticalCenter: parent.verticalCenter
                    }
                    Image {
                        source: "qrc:/img/close.png"
                        anchors.centerIn: parent

                        MouseArea {
                            id: closeButtonMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                tabBarCpp.closeAtIndex(index);
                            }
                        }
                    }
                }
            }
        }

        Connections {
            target: tabBarCpp

            function onHoveredTabIndexChanged(index) {
                tabBarRow.hoveredIndex = index;
            }
        }
    }
}
