import QtQuick 2.5
import QtQuick.Window 2.13
import TGFX 1.0

Window {
    id: mainWindow
    visible: true
    width: 800
    height: 600

    property real currentZoom: 1.0
    property point currentOffset: Qt.point(0, 0)
    property point mousePosition: Qt.point(width / 2, height / 2)

    readonly property real minZoom: 0.001
    readonly property real maxZoom: 1000.0
    readonly property real mouseScaleRatio: 300.0
    readonly property real mouseScrollRatio: 0.8
    readonly property real devicePixelRatio: mainWindow.Screen.devicePixelRatio || 1.0

    function calculateTransform(scaleFactor) {
        var newZoom = Math.max(minZoom, Math.min(maxZoom, currentZoom * scaleFactor))
        var px = mousePosition.x * devicePixelRatio
        var py = mousePosition.y * devicePixelRatio
        currentOffset.x = (currentOffset.x - px) * (newZoom / currentZoom) + px
        currentOffset.y = (currentOffset.y - py) * (newZoom / currentZoom) + py
        currentZoom = newZoom
    }

    PinchArea {
        id: pinchArea
        anchors.fill: parent

        property real lastScale: 1.0

        onPinchStarted: {
            lastScale = 1.0
        }

        onPinchUpdated: function(pinch) {
            var scaleDelta = pinch.scale / lastScale
            calculateTransform(scaleDelta)
            tgfxView.updateTransform(currentZoom, currentOffset)
            lastScale = pinch.scale
        }

        TGFXView {
            id: tgfxView
            anchors.fill: parent
            objectName: "tgfxView"
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton

            onPositionChanged: function(mouse) {
                mousePosition = Qt.point(mouse.x, mouse.y)
            }

            onClicked: function(mouse) {
                tgfxView.onClicked()
                currentZoom = 1.0
                currentOffset = Qt.point(0, 0)
            }

            onWheel: function(wheel) {
                var isZoomMode = (wheel.modifiers & Qt.ControlModifier) || (wheel.modifiers & Qt.MetaModifier)
                var isShiftPressed = wheel.modifiers & Qt.ShiftModifier
                if (isZoomMode) {
                    var scaleFactor = Math.exp(wheel.angleDelta.y / mouseScaleRatio)
                    calculateTransform(scaleFactor)
                } else {
                    var deltaX = wheel.angleDelta.x * devicePixelRatio * mouseScrollRatio
                    var deltaY = wheel.angleDelta.y * devicePixelRatio * mouseScrollRatio
                    if (isShiftPressed && deltaX === 0 && deltaY !== 0) {
                        deltaX = deltaY
                        deltaY = 0
                    }
                    currentOffset = Qt.point(currentOffset.x + deltaX, currentOffset.y + deltaY)
                }
                tgfxView.updateTransform(currentZoom, currentOffset)
                wheel.accepted = true
            }
        }
    }
}