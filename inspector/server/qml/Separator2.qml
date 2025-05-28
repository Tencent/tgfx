import QtQuick 2.6

Rectangle {
    id: root
    anchors.fill: parent
    color: "#535353"
    width: parent && kddwSeparator && kddwSeparator.isVertical ? 5 : (parent ? parent.width : 5)
    height: parent && kddwSeparator && !kddwSeparator.isVertical ? 5 : (parent ? parent.height : 5)
    z: 2

    readonly property QtObject kddwSeparator: parent

    MouseArea {
        cursorShape: kddwSeparator ? (kddwSeparator.isVertical ? Qt.SizeVerCursor : Qt.SizeHorCursor)
            : Qt.SizeHorCursor

        readonly property int hoverThreshold: 5
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: parent.bottom
            leftMargin: (kddwSeparator && !kddwSeparator.isVertical) ? -hoverThreshold : 0
            rightMargin: (kddwSeparator && !kddwSeparator.isVertical) ? -hoverThreshold : 0
            topMargin: (kddwSeparator && kddwSeparator.isVertical) ? -hoverThreshold : 0
            bottomMargin: (kddwSeparator && kddwSeparator.isVertical) ? -hoverThreshold : 0
        }



        onPressed: {
            kddwSeparator.onMousePressed();
        }

        onReleased: {
            kddwSeparator.onMouseReleased();
        }

        onPositionChanged: (mouse) => {
            kddwSeparator.onMouseMoved(Qt.point(mouse.x, mouse.y));
        }
    }
}
