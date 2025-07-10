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

    function calculateZoomWithCenter(oldZoom, scaleFactor, centerX, centerY) {
        var ratio = mainWindow.Screen.devicePixelRatio || 1.0
        var newZoom = Math.max(minZoom, Math.min(maxZoom, oldZoom * scaleFactor))
        var px = centerX * ratio
        var py = centerY * ratio

        var newOffsetX = (currentOffset.x - px) * (newZoom / oldZoom) + px
        var newOffsetY = (currentOffset.y - py) * (newZoom / oldZoom) + py

        return {
            zoom: newZoom,
            offset: Qt.point(newOffsetX, newOffsetY)
        }
    }

    function resetTransform() {
        currentZoom = 1.0
        currentOffset = Qt.point(0, 0)
        tgfxView.resetView()
    }

    function updateViewTransform() {
        tgfxView.updateTransform(currentZoom, currentOffset)
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
            var result = calculateZoomWithCenter(currentZoom, scaleDelta, mousePosition.x, mousePosition.y)

            currentZoom = result.zoom
            currentOffset = result.offset
            updateViewTransform()

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

            onClicked: function(mouse) {
                tgfxView.nextDrawer()
                resetTransform()
            }

            onWheel: function(wheel) {
                var ratio = mainWindow.Screen.devicePixelRatio || 1.0
                var isZoomMode = (wheel.modifiers & Qt.ControlModifier) || (wheel.modifiers & Qt.MetaModifier)
                var isShiftPressed = wheel.modifiers & Qt.ShiftModifier

                if (isZoomMode) {
                    var scaleFactor = Math.exp(wheel.angleDelta.y / mouseScaleRatio)
                    var result = calculateZoomWithCenter(currentZoom, scaleFactor, wheel.x, wheel.y)
                    currentZoom = result.zoom
                    currentOffset = result.offset
                    updateViewTransform()
                } else {
                    var deltaX = wheel.angleDelta.x * ratio * mouseScrollRatio
                    var deltaY = wheel.angleDelta.y * ratio * mouseScrollRatio

                    if (isShiftPressed && deltaX === 0 && deltaY !== 0) {
                        deltaX = deltaY
                        deltaY = 0
                    }

                    currentOffset = Qt.point(currentOffset.x + deltaX, currentOffset.y + deltaY)
                    updateViewTransform()
                }
                wheel.accepted = true
            }
        }
    }
}
