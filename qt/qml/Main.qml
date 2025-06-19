import QtQuick 2.5
import QtQuick.Window 2.13
import TGFX 1.0

Window {
    id: wind
    visible: true
    width: 800
    height: 600

    PinchArea {
        id: pinchArea
        anchors.fill: parent
        property real lastScale: 1.0
        onPinchStarted: {
            lastScale = 1.0
        }
        onPinchUpdated: {
            tgfxView.handlePinch(pinch.scale / lastScale, pinch.center)
            lastScale = pinch.scale
        }
        TGFXView {
            id: tgfxView
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            objectName: "tgfxView"
            focus: true
        }
    }
}
