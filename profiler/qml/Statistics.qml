import QtQuick 2.15
import QtQuick.Controls.Basic
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import TGFX.Profiler 1.0
import TGFX.Controls 1.0


Window {
    id: root
    visible: true
    width: 1920
    height: 800
    title: "Statistics View"

    readonly property real nameColumnRatio: 0.35
    readonly property real locationColumnRatio: 0.35
    readonly property real flaexibleColumnsRatio: nameColumnRatio + locationColumnRatio

    readonly property int totalTimeWidth: 150
    readonly property int countWidth: 80
    readonly property int mtpcWidth: 130
    readonly property int threadsWidth: 70

    readonly property int totalFixedWidth: totalTimeWidth + countWidth + mtpcWidth + threadsWidth
    readonly property int rowSpacing: 1

    property int availableWidth: Math.max(0, tableContainer.width - totalFixedWidth - 5 * rowSpacing)
    property int nameColumnWidth: Math.max(100, Math.floor(availableWidth * (nameColumnRatio / flexibleColumnRadio)))
    property int locationColumnWidth: Math.max(100, availableWidth - nameColumnWidth)

    property int sortedColumn: 0
    property int sortedOrder: Qt.AscendingOrder

    property var model: statisticsModel

    function updateFlexibleColumns() {
        nameColumnWidth = Math.max(100, Math.floor(availableWidth * (nameColumnRatio / flaexibleColumnsRatio)))
        locationColumnWidth = Math.max(100, availableWidth - nameColumnWidth)
    }

    onAvailableWidthChanged: {
        updateFlexibleColumns();
    }

    Component.onCompleted: {
        Qt.callLater(updateFlexibleColumns);

        totalTimeHeader.isSorted = true;
        totalTimeHeader.sortAscending = false;

        model.sort(columns.totalTime, Qt.DescendingOrder);
        sortedColumn = columns.totalTime;
        sortedOrder = Qt.DescendingOrder;
    }

    readonly property var columns: {
        "name": 0,
        "location": 1,
        "totalTime": 2,
        "count": 3,
        "mtpc": 4,
        "threads": 5
    }

    /////*statistics main view*/////
    Rectangle {
        anchors.fill: parent
        color: "#343131"

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            ///////*mode comboBox and limit*//////
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                color: "#2D2D2D"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 10

                    RadioButton {
                        id: instrumentationBtn
                        text: "Instrumentation"
                        checked: true
                        contentItem: Text {
                            text: instrumentationBtn.text
                            color: "white"
                            leftPadding: instrumentationBtn.indicator.width + 4
                            verticalAlignment: Text.AlignVCenter
                            x:instrumentationBtn.indicator.width + 4
                        }
                        onCheckedChanged: {
                            if (checked) {
                                model.setStatisticsMode(0)
                            }
                        }
                    }

                    ToolSeparator {
                        anchors.verticalCenter: Layout.verticalCenter
                        orientation: Qt.Vertical
                        contentItem: Rectangle {
                            implicitHeight: Layout.vertical ? 1 : 24
                            implicitWidth: Layout.vertical ? 24 : 1
                            color: "#666666"
                        }
                    }

                    Text {
                        text: "Total zone count:"
                        color: "white"
                    }

                    Text {
                        id: totalZoneCount
                        text: model.totalZoneCount
                        color: "white"
                    }

                    Text {
                        text: "Visible zones:"
                        color: "white"
                    }

                    Text {
                        id: visibleZones
                        text: model.visibleZoneCount
                        color: "white"
                    }

                    ToolSeparator {
                        anchors.verticalCenter: Layout.verticalCenter
                        orientation: Qt.Vertical
                        contentItem: Rectangle {
                            implicitHeight: Layout.vertical ? 1 : 24
                            implicitWidth: Layout.vertical ? 24 : 1
                            color: "#666666"
                        }
                    }

                    Text {
                        text: "Timing"
                        color: "white"
                    }

                    ComboBox {
                        id: timingCombo
                        Layout.preferredWidth: 150
                        model: [ "Self Only", "With Children", "Non-reentrant" ]
                        currentIndex: 0

                        contentItem: Text {
                            text: timingCombo.displayText
                            color: "white"
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: "#404040"
                            border.color: "#555555"
                            border.width: 1
                        }

                        onCurrentIndexChanged: {
                            if (model) {
                                root.model.setAccumulationMode(currentIndex)
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ToolSeparator {
                        anchors.verticalCenter: Layout.verticalCenter
                        orientation: Qt.Vertical
                        contentItem: Rectangle {
                            implicitHeight: Layout.vertical ? 1 : 24
                            implicitWidth: Layout.vertical ? 24 : 1
                            color: "#666666"
                        }
                    }

                    Text {
                        text: "Limit"
                        color: "white"
                    }

                    Switch {
                        id: limitRange
                        onCheckedChanged: {
                            model.rangeActive = limitRange.checked
                            color = limitRange.checked ? "#666666" : "#333333"
                        }
                    }
                }
            }

            /////*filter text toolbar*/////
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: "#2D2D2D"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Text {
                        text: "Filter"
                        color: "white"
                    }

                    TextField {
                        id: filterEdit
                        Layout.fillWidth: true
                        placeholderText: "Enter filter text (use -term to exclude, name:term or file:term for field search, /regex/ for regex)..."
                        text: model.filterText
                        color: "white"

                        background: Rectangle {
                            color: "#404040"
                            border.color: "#555555"
                            border.width: 1
                        }

                        onTextChanged: {
                            model.filterText = text
                        }
                    }

                    Button {
                        text: "Clear"
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.hovered ? "#505050" : "#404040"
                            border.color: "#555555"
                            border.width: 1
                        }
                        onClicked: {
                            model.clearFilter()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 0

                ColumnLayout {
                    Layout.margins: 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Rectangle {
                        anchors.margins: 0
                        id: fpsChartHeadTabBar
                        Layout.preferredHeight: 100
                        Layout.preferredWidth: 200
                        border.width: 0.5
                        border.color: "#4D4D4D"
                        color: "#2D2D2D"
                        Text {
                            id: fpsHeadText
                            text: "FPS"

                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.leftMargin: 10
                            anchors.topMargin: 8

                            color: "white"
                            font.bold: true
                        }
                    }

                    Rectangle {
                        anchors.margins: 0
                        id: drawCallChartHeadTabBar
                        Layout.preferredHeight: 100
                        Layout.preferredWidth: 200
                        border.width: 0.5
                        border.color: "#4D4D4D"
                        color: "#2D2D2D"
                        Text {
                            id: drawCallHeadText
                            text: "DrawCall"

                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.leftMargin: 10
                            anchors.topMargin: 8

                            color: "white"
                            font.bold: true
                        }
                    }

                    Rectangle {
                        anchors.margins: 0
                        id: tranglesChartHeadTabBar
                        Layout.preferredHeight: 100
                        Layout.preferredWidth: 200
                        border.width: 0.5
                        border.color: "#4D4D4D"
                        color: "#2D2D2D"
                        Text {
                            id: trianglesHeadText
                            text: "Triangles"

                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.leftMargin: 10
                            anchors.topMargin: 8

                            color: "white"
                            font.bold: true
                        }
                    }
                }

                Rectangle {
                    id: chartTabBar
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    anchors.margins: 0
                    color: "#2D2D2D"

                    MouseArea {
                        id: mouseArea
                        propagateComposedEvents: true
                        hoverEnabled: true
                        anchors.fill: parent

                        onPositionChanged: function(mouse) {
                            line.x = mouse.x
                        }
                        onExited: {
                            line.visible = false
                        }
                        onEntered: {
                            line.visible = true
                        }

                        ColumnLayout {
                            anchors.margins: 0
                            anchors.fill: parent
                            spacing: 0

                            FPSChart {
                                id: fpsChart
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                dataName: "FPS"
                                model: statisticsModel
                            }

                            DrawCallChart {
                                id: drawCallChart
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                dataName: "DrawCall"
                                model: statisticsModel
                            }

                            TriangleChart {
                                id: triangleChart
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                dataName: "Triangles"
                                model: statisticsModel
                            }
                        }
                    }

                    Rectangle {
                        id: line
                        x: mouseArea.width / 2
                        width: 1
                        height: parent.height
                        color: "#6D6D6D"
                    }
                }
            }

            /////*table view header*/////
            Item {
                id: tableContainer
                Layout.fillWidth: true
                Layout.fillHeight: true

                Row {
                    id: headerRow
                    width: parent.width
                    height: 30
                    spacing: 1

                    Rectangle {
                        id: nameHeader
                        width: nameColumnWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"
                        border.width: 1

                        property bool isSorted: false
                        property bool sortAscending: true

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "Name"
                                color: "white"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                visible: nameHeader.isSorted
                                text: nameHeader.sortAscending ? "▲" : "▼"
                                color: "#BD94AB"
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (nameHeader.isSorted) {
                                    nameHeader.sortAscending = !nameHeader.sortAscending;
                                }
                                else {
                                    locationHeader.isSorted = false;
                                    totalTimeHeader.isSorted = false;
                                    countHeader.isSorted = false;
                                    mtpcHeader.isSorted = false;
                                    threadsHeader.isSorted = false;

                                    nameHeader.isSorted = true;
                                    nameHeader.sortAscending = true;
                                }

                                root.sortedColumn = columns.name
                                root.sortedOrder = nameHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder

                                model.sort(
                                    columns.name,
                                    nameHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                                );
                            }
                        }

                        Rectangle {
                            width: 4
                            height: parent.height
                            anchors.right: parent.right
                            color: "transparent"

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.SizeHorCursor
                                property int startX
                                property int startNameWidth
                                property int startLocationWidth

                                onPressed: {
                                    startX = mouseX;
                                    startNameWidth = nameColumnWidth;
                                    startLocationWidth = locationColumnWidth;
                                }

                                onPositionChanged: {
                                    if (pressed) {
                                        var delta = mouseX - startX;

                                        var newNameWidth = Math.max(60, startNameWidth + delta);
                                        var newLocationWidth = Math.max(60, availableWidth - newNameWidth);

                                        if (newNameWidth <= availableWidth - 60) {
                                            nameColumnWidth = newNameWidth;
                                            locationColumnWidth = newLocationWidth;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: locationHeader
                        width: locationColumnWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"
                        border.width: 1

                        property bool isSorted: false
                        property bool sortAscending: true

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "Location"
                                color: "white"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                visible: locationHeader.isSorted
                                text: locationHeader.sortAscending ? "▲" : "▼"
                                color: "#BD94AB"
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (locationHeader.isSorted) {
                                    locationHeader.sortAscending = !locationHeader.sortAscending;
                                } else {
                                    nameHeader.isSorted = false;
                                    totalTimeHeader.isSorted = false;
                                    countHeader.isSorted = false;
                                    mtpcHeader.isSorted = false;
                                    threadsHeader.isSorted = false;

                                    locationHeader.isSorted = true;
                                    locationHeader.sortAscending = true;
                                }

                                root.sortedColumn = columns.location
                                root.sortedOrder = locationHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder

                                model.sort(
                                    columns.location,
                                    locationHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                                );
                            }
                        }
                    }

                    Rectangle {
                        id: totalTimeHeader
                        width: totalTimeWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"
                        border.width: 1

                        property bool isSorted: false
                        property bool sortAscending: false

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "Total Time"
                                color: "white"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                visible: totalTimeHeader.isSorted
                                text: totalTimeHeader.sortAscending ? "▲" : "▼"
                                color: "#BD94AB"
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (totalTimeHeader.isSorted) {
                                    totalTimeHeader.sortAscending = !totalTimeHeader.sortAscending;
                                } else {
                                    nameHeader.isSorted = false;
                                    locationHeader.isSorted = false;
                                    countHeader.isSorted = false;
                                    mtpcHeader.isSorted = false;
                                    threadsHeader.isSorted = false;
                                    totalTimeHeader.isSorted = true;
                                    totalTimeHeader.sortAscending = false;
                                }

                                root.sortedColumn = columns.totalTime
                                root.sortedOrder = totalTimeHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder

                                model.sort(
                                    columns.totalTime,
                                    totalTimeHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                                );
                            }
                        }
                    }

                    Rectangle {
                        id: countHeader
                        width: countWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"
                        border.width: 1

                        property bool isSorted: false
                        property bool sortAscending: true

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "Count"
                                color: "white"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                visible: countHeader.isSorted
                                text: countHeader.sortAscending ? "▲" : "▼"
                                color: "#BD94AB"
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (countHeader.isSorted) {
                                    countHeader.sortAscending = !countHeader.sortAscending;
                                } else {
                                    nameHeader.isSorted = false;
                                    locationHeader.isSorted = false;
                                    totalTimeHeader.isSorted = false;
                                    mtpcHeader.isSorted = false;
                                    threadsHeader.isSorted = false;
                                    countHeader.isSorted = true;
                                    countHeader.sortAscending = false;
                                }

                                root.sortedColumn = columns.count
                                root.sortedOrder = countHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder

                                model.sort(
                                    columns.count,
                                    countHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                                );
                            }
                        }
                    }

                    Rectangle {
                        id: mtpcHeader
                        width: mtpcWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"
                        border.width: 1

                        property bool isSorted: false
                        property bool sortAscending: true

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "MTPC"
                                color: "white"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                visible: mtpcHeader.isSorted
                                text: mtpcHeader.sortAscending ? "▲" : "▼"
                                color: "#BD94AB"
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (mtpcHeader.isSorted) {
                                    mtpcHeader.sortAscending = !mtpcHeader.sortAscending;
                                } else {
                                    nameHeader.isSorted = false;
                                    locationHeader.isSorted = false;
                                    totalTimeHeader.isSorted = false;
                                    countHeader.isSorted = false;
                                    threadsHeader.isSorted = false;
                                    mtpcHeader.isSorted = true;
                                    mtpcHeader.sortAscending = false;
                                }

                                root.sortedColumn = columns.mtpc
                                root.sortedOrder = mtpcHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder

                                model.sort(
                                    columns.mtpc,
                                    mtpcHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                                );
                            }
                        }
                    }

                    Rectangle {
                        id: threadsHeader
                        width: threadsWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"
                        border.width: 1

                        property bool isSorted: false
                        property bool sortAscending: true

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "Threads"
                                color: "white"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                visible: threadsHeader.isSorted
                                text: threadsHeader.sortAscending ? "▲" : "▼"
                                color: "#BD94AB"
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (threadsHeader.isSorted) {
                                    threadsHeader.sortAscending = !threadsHeader.sortAscending;
                                } else {
                                    nameHeader.isSorted = false;
                                    locationHeader.isSorted = false;
                                    totalTimeHeader.isSorted = false;
                                    countHeader.isSorted = false;
                                    mtpcHeader.isSorted = false;

                                    threadsHeader.isSorted = true;
                                    threadsHeader.sortAscending = false;
                                }

                                root.sortedColumn = columns.threads
                                root.sortedOrder = threadsHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder

                                model.sort(
                                    columns.threads,
                                    threadsHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                                );
                            }
                        }
                    }
                }

                /////*table view*/////
                ListView {
                    id: listView
                    anchors.top: headerRow.bottom
                    width: parent.width
                    height: parent.height - headerRow.height
                    model: root.model
                    spacing: 1
                    clip: true

                    delegate: Rectangle {
                        id: rowDelegate
                        width: listView.width
                        height: 36
                        color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"

                        Row {
                            width: parent.width
                            height: parent.height
                            spacing: 1

                        Rectangle {
                            width: nameColumnWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            border.color: "#555555"
                            border.width: 1
                            clip: true

                                Rectangle {
                                    id: statusIcon
                                    width: 16
                                    height: 16
                                    radius: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    color: model.color || "#808080"
                                    border.color: "#c8c8c8c8"
                                    border.width: 1
                                }

                                TGFXText {
                                    id: nameText
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: statusIcon.right
                                    anchors.leftMargin: 8
                                    width: parent.width - statusIcon.width - 24
                                    height:parent.height
                                    clip: true
                                    text: model.Name
                                    color: "white"
                                    elideMode: Qt.ElideRight
                                    alignment: Qt.AlignLeft | Qt.AlignVCenter
                                }
                            }

                        Rectangle {
                            width: locationColumnWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            border.color: "#555555"
                            border.width: 1
                            clip: true

                                TGFXText {
                                    id: locationText
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    width: parent.width - 16
                                    height: parent.height
                                    clip: true
                                    text: model.Location
                                    color: "white"
                                    elideMode: Qt.ElideMiddle
                                    alignment: Qt.AlignLeft | Qt.AlignVCenter
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true

                                    onDoubleClicked: {
                                        root.model.openSource(index)
                                    }
                                }
                            }

                        Rectangle {
                            width: totalTimeWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            border.color: "#555555"
                            border.width: 1
                            clip: true

                            TGFXText {
                                id: totalTimeText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                text: model.Totaltime
                                color: "white"
                                height: parent.height
                                width: model.percentage !== undefined ? 70 : parent.width - 16
                                alignment: Qt.AlignLeft | Qt.AlignVCenter
                                elideMode: Qt.ElideNone
                            }

                            TGFXText {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: totalTimeText.right
                                anchors.leftMargin: 4
                                text: {
                                    if(model.percentage !== undefined) {
                                        return "(" + model.percentage.toFixed(2) + "%)"
                                    }
                                    return ""
                                }
                                visible: model.percentage !== undefined
                                color: "#FFFFFF80"
                                height: parent.height
                                width: 70
                                alignment: Qt.AlignLeft | Qt.AlignVCenter
                                elideMode:Qt.ElideNone
                            }

                        }

                        Rectangle {
                            width: countWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            border.color: "#555555"
                            border.width: 1
                            clip: true

                                TGFXText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    text: model.Count
                                    color: "white"
                                    height: parent.height
                                    width: parent.width - 16
                                    alignment: Qt.AlignLeft | Qt.AlignVCenter
                                    elideMode: Qt.ElideNone
                                }
                            }

                        Rectangle {
                            width: mtpcWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            border.color: "#555555"
                            border.width: 1
                            clip: true

                                TGFXText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    text: model.Mtpc
                                    color: "white"
                                    height: parent.height
                                    width: parent.width - 16
                                    alignment: Qt.AlignLeft | Qt.AlignVCenter
                                    elideMode: Qt.ElideNone
                                }
                            }

                        Rectangle {
                            width: threadsWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            border.color: "#555555"
                            border.width: 1
                            clip: true

                                TGFXText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    text: model.Threadcount
                                    color: "white"
                                    height: parent.height
                                    width: parent.width - 16
                                    alignment: Qt.AlignLeft | Qt.AlignVCenter
                                    elideMode: Qt.ElideNone
                                }
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        width: 12
                        policy: ScrollBar.AsNeeded
                        active: true

                        background: Rectangle {
                            color: "#2D2D2D"
                        }

                        contentItem: Rectangle {
                            implicitWidth: 6
                            implicitHeight: 100
                            radius: width / 2
                            color: parent.pressed ? "#BD94AB" : "#808080"
                        }
                    }
                }
            }
        }
    }


    Connections {
        target: model
        function onZoneCountChanged() {
            totalZoneCount.text = model.totalZoneCount
            visibleZones.text = model.visibleZoneCount
        }

        function onFilterTextChanged() {
            filterEdit.text = model.filterText
        }

        function onStatisticsUpdated() {
            listView.forceLayout()
            if(root.sortedColumn !== -1) {
                model.sort(root.sortedColumn, root.sortedOrder)
            }
        }

        function onAccumulationModeChanged() {
            timingCombo.currentIndex = model.accumulationMode
        }
    }
}
