/*
  This file is part of KDDockWidgets.

  SPDX-FileCopyrightText: 2020 Klarälvdalens Datakonsult AB, a KDAB Group company <info@kdab.com>
  Author: Sérgio Martins <sergio.martins@kdab.com>

  SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only

  Contact KDAB at <info@kdab.com> for commercial licensing options.
*/

import QtQuick 2.6

Rectangle {
    id: root
    anchors.fill: parent
    color: "#535353"
    width: parent && kddwSeparator && kddwSeparator.isVertical ? 5 : (parent ? parent.width : 5)
    height: parent && kddwSeparator && !kddwSeparator.isVertical ? 5 : (parent ? parent.height : 5)

    // Might be needed as MouseArea overlaps with the dock widgets, since it's bigger than
    // its parent.
    z: 2

    readonly property QtObject kddwSeparator: parent

    MouseArea {
        cursorShape: kddwSeparator ? (kddwSeparator.isVertical ? Qt.SizeVerCursor : Qt.SizeHorCursor)
            : Qt.SizeHorCursor

        // 将分割条大小改为5px，同时调整鼠标悬停区域
        readonly property int hoverThreshold: 5  // 与分割条大小保持一致
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

        onDoubleClicked: {
            kddwSeparator.onMouseDoubleClicked();
        }
    }
}
