import QtQuick 2.5
import QtQuick.Window 2.13
import TGFX 1.0

Window {
    id: wind
    visible: true
    width: 800
    height: 600
    property point mousePos: Qt.point(width, height)

    PinchArea {
        id: pinchArea
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        property real lastScale: 1.0
        onPinchStarted: lastScale = 1.0
        onPinchUpdated: {
            tgfxView.handlePinch(pinch.scale / lastScale, wind.mousePos)
            lastScale = pinch.scale
        }

        TGFXView {
            id: tgfxView
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            objectName: "tgfxView"
            focus: true
        }

        MouseArea {
            id: mouseTracker
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            hoverEnabled: true
            onPositionChanged: function (mouse) {
                wind.mousePos = Qt.point(mouse.x, mouse.y)
            }
            onClicked: function (mouse) {
                tgfxView.onMouseClicked(mouse.x, mouse.y)
            }
        }
    }
}
