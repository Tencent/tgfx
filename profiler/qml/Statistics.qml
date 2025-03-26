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

    readonly property int totalTimeWidth: 160
    readonly property int countWidth: 80
    readonly property int mtpcWidth: 130
    readonly property int threadsWidth: 70

    readonly property int totalFixedWidth: totalTimeWidth + countWidth + mtpcWidth + threadsWidth
    readonly property int rowSpacing: 0

    property int availableWidth: Math.max(0, tableContainer.width - totalFixedWidth - 5 * rowSpacing)
    property int nameColumnWidth: Math.max(100, Math.floor(availableWidth * (nameColumnRatio / flaexibleColumnsRatio)))
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

        canvasTable.sortColumn = columns.totalTime
        canvasTable.sortOrder = Qt.DescendingOrder

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
        color: "#2D2D2D"

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
                            font.bold: true
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

                    Item { Layout.preferredWidth: 5 }

                    Text {
                        text: "Total zone count:"
                        color: "white"
                        font.bold: true
                    }

                    Item { Layout.preferredWidth: 2 }

                    Text {
                        id: totalZoneCount
                        text: model.totalZoneCount
                        color: "white"
                        font.bold: true
                        Layout.preferredWidth: 5
                        horizontalAlignment: Text.AlignRight
                    }

                    Item { Layout.preferredWidth: 5 }

                    Text {
                        text: "Visible zones:"
                        color: "white"
                        font.bold: true
                    }

                    Item { Layout.preferredWidth: 2 }

                    Text {
                        id: visibleZones
                        text: model.visibleZoneCount
                        color: "white"
                        font.bold: true
                        Layout.preferredWidth: 5
                        horizontalAlignment: Text.AlignRight
                    }

                    Item { Layout.preferredWidth: 5 }

                    ToolSeparator {
                        anchors.verticalCenter: Layout.verticalCenter
                        orientation: Qt.Vertical
                        contentItem: Rectangle {
                            implicitHeight: Layout.vertical ? 1 : 24
                            implicitWidth: Layout.vertical ? 24 : 1
                            color: "#666666"
                        }
                    }

                    Item { Layout.preferredWidth: 5 }

                    Row {
                        spacing: 3

                        Text {
                            text: "Frame :"
                            color: "white"
                            font.bold: true
                            width: 60
                        }

                        Text {
                            id: frameRangeText
                            text: model.getFirstFrame() + " - " + model.getLastFrame()
                            color: "white"
                            font.bold: true
                            width: 70
                        }
                    }

                    //Item { Layout.preferredWidth: 5 }

                    ToolSeparator {
                        anchors.verticalCenter: Layout.verticalCenter
                        orientation: Qt.Vertical
                        contentItem: Rectangle {
                            implicitHeight: Layout.vertical ? 1 : 24
                            implicitWidth: Layout.vertical ? 24 : 1
                            color: "#666666"
                        }
                    }

                    Item { Layout.preferredWidth: 5 }

                    Text {
                        text: "Timing"
                        color: "white"
                        font.bold: true
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

                    Item { Layout.preferredWidth: 5 }

                    ToolSeparator {
                        anchors.verticalCenter: Layout.verticalCenter
                        orientation: Qt.Vertical
                        contentItem: Rectangle {
                            implicitHeight: Layout.vertical ? 1 : 24
                            implicitWidth: Layout.vertical ? 24 : 1
                            color: "#666666"
                        }
                    }

                    Item { Layout.preferredWidth: 5 }

                    Text {
                        text: "Limit"
                        color: "white"
                        font.bold: true
                    }

                    Switch {
                        id: limitRange
                        onCheckedChanged: {
                            model.rangeActive = limitRange.checked
                            color = limitRange.checked ? "#666666" : "#333333"
                        }
                    }

                    Item { Layout.fillWidth: true }
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

                        Column {
                            anchors.left: parent.left
                            anchors.top: fpsHeadText.bottom
                            anchors.leftMargin: 10
                            anchors.topMargin: 8
                            spacing: 5

                            Text {
                                id: maxFps
                                text: "MaxFps : " + model.getMaxFps().toFixed(1)
                                color: "white"
                                font.pixelSize: 15
                            }

                            Text {
                                id: minFps
                                text: "MinFps : " + model.getMinFps().toFixed(1)
                                color: "white"
                                font.pixelSize: 15
                            }

                            Text {
                                id: avgFps
                                text: "AvgFps : " + model.getAvgFps().toFixed(1)
                                color: "white"
                                font.pixelSize: 15
                            }
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

                        Column {
                            anchors.left: parent.left
                            anchors.top: drawCallHeadText.bottom
                            anchors.leftMargin: 10
                            anchors.topMargin: 8
                            spacing: 5

                            Text {
                                id: maxDrawCall
                                text: "MaxDrawCall : " + model.getMaxDrawCall().toFixed(1)
                                color: "white"
                                font.pixelSize: 15
                            }

                            Text {
                                id: minDrawCall
                                text: "MaxDrawCall : " + model.getMinDrawCall().toFixed(1)
                                color: "white"
                                font.pixelSize: 15
                            }
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

                        Column {
                            anchors.left: parent.left
                            anchors.top: trianglesHeadText.bottom
                            anchors.leftMargin: 10
                            anchors.topMargin: 8
                            spacing: 5

                            Text {
                                id: maxTriangles
                                text: "MaxTriangles : " + model.getMaxTriangles().toFixed(1)
                                color: "white"
                                font.pixelSize: 15
                            }

                            Text {
                                id: minTriangles
                                text: "MinTriangles : " + model.getMinTriangles().toFixed(1)
                                color: "white"
                                font.pixelSize: 15
                            }
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
                    spacing: rowSpacing
                    anchors.margins: 0

                    Rectangle {
                        id: nameHeader
                        width: nameColumnWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"

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

                                canvasTable.sortColumn = columns.name
                                canvasTable.sortOrder = nameHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
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

                                canvasTable.sortColumn = columns.location
                                canvasTable.sortOrder = locationHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                            }
                        }
                    }

                    Rectangle {
                        id: totalTimeHeader
                        width: totalTimeWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"

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

                                canvasTable.sortColumn = columns.totalTime
                                canvasTable.sortOrder = totalTimeHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                            }
                        }
                    }

                    Rectangle {
                        id: countHeader
                        width: countWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"

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

                                canvasTable.sortColumn = columns.count
                                canvasTable.sortOrder = countHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                            }
                        }
                    }

                    Rectangle {
                        id: mtpcHeader
                        width: mtpcWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"

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

                                canvasTable.sortColumn = columns.mtpc
                                canvasTable.sortOrder = mtpcHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                            }
                        }
                    }

                    Rectangle {
                        id: threadsHeader
                        width: threadsWidth
                        height: 30
                        color: "#2D2D2D"
                        border.color: "#555555"

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

                                canvasTable.sortColumn = columns.threads
                                canvasTable.sortOrder = threadsHeader.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder
                            }
                        }
                    }
                }

                //////*Table View*//////
                TGFXTable {
                    id: canvasTable
                    anchors.top: headerRow .bottom
                    width: parent.width
                    height: parent.height - headerRow.height
                    model: statisticsModel
                    rowHeight: 36
                    nameColumnWidth: root.nameColumnWidth
                    locationColumnWidth: root.locationColumnWidth
                    totalTimeWidth: root.totalTimeWidth
                    countWidth: root.countWidth
                    mtpcWidth: root.mtpcWidth
                    threadsWidth: root.threadsWidth

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true

                        onDoubleClicked: function(mouse) {
                            canvasTable.handleMouseDoubleClick(mouse.x, mouse.y)
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
            frameRangeText.text = model.getFirstFrame() + " - " + model.getLastFrame()
        }
        function onFilterTextChanged() {
            filterEdit.text = model.filterText
        }
        function onStatisticsUpdated() {
            maxFps.text = "MaxFps: " + model.getMaxFps().toFixed(1)
            minFps.text = "MinFps: " + model.getMinFps().toFixed(1)
            avgFps.text = "AvgFps: " + model.getAvgFps().toFixed(1)
            maxDrawCall.text = "MaxDrawCall: " + model.getMaxDrawCall().toFixed(1)
            minDrawCall.text = "MinDrawCall: " + model.getMinDrawCall().toFixed(1)
            maxTriangles.text = "MaxTriangles: " + model.getMaxTriangles().toFixed(1)
            minTriangles.text = "MinTriangles: " + model.getMinTriangles().toFixed(1)
            if(root.sortedColumn !== -1) {
                canvasTable.sortColumn = root.sortedColumn
                canvasTable.sortOrder = root.sortedOrder
            }
        }
        function onAccumulationModeChanged() {
            timingCombo.currentIndex = model.accumulationMode
        }
        function onRangeActiveChanged() {
            maxFps.text = "MaxFps: " + model.getMaxFps().toFixed(1)
            minFps.text = "MinFps: " + model.getMinFps().toFixed(1)
            avgFps.text = "AvgFps: " + model.getAvgFps().toFixed(1)
            maxDrawCall.text = "MaxDrawCall: " + model.getMaxDrawCall().toFixed(1)
            minDrawCall.text = "MinDrawCall: " + model.getMinDrawCall().toFixed(1)
            maxTriangles.text = "MaxTriangles: " + model.getMaxTriangles().toFixed(1)
            minTriangles.text = "MinTriangles: " + model.getMinTriangles().toFixed(1)
        }
    }
}
