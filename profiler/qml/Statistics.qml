import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import TGFX.Profiler 1.0

Window {
    id: root
    visible: true
    width: _width
    height: 800
    title: "Statistics View"

    property int nameColumnWidth: 1000
    property int locationColumnWidth: 900

    readonly property int totalTimeWidth: 120
    readonly property int countWidth: 80
    readonly property int mtpcWidth: 100
    readonly property int threadsWidth: 60

    property int totalFixedWidth: totalTimeWidth + countWidth + mtpcWidth + threadsWidth
    property int availableWidth: tableContainer.width - totalFixedWidth

    function updateFlexibleColumns() {
        if (availableWidth <= 0) return;

        var currentFlexWidth = nameColumnWidth + locationColumnWidth;

        if (currentFlexWidth < availableWidth) {
             currentFlexWidth;
        } else {
            var ratio = availableWidth / currentFlexWidth;
            nameColumnWidth = Math.max(100, Math.floor(nameColumnWidth * ratio));
            locationColumnWidth = Math.max(100, availableWidth - nameColumnWidth);
        }
    }


    onAvailableWidthChanged: {
        updateFlexibleColumns();
    }


    Component.onCompleted: {
        Qt.callLater(updateFlexibleColumns);
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
                        }
                        onCheckedChanged: {
                            if (checked) {
                                statisticsView.model.setStatisticsMode(0)
                            }
                        }
                    }

                    Text {
                        text: "|"
                        color: "#666666"
                    }

                    Text {
                        text: "Total zone count:"
                        color: "white"
                    }
                    Text {
                        id: totalZoneCount
                        text: statisticsView.totalZoneCount
                        color: "white"
                    }

                    Text {
                        text: "Visible zones:"
                        color: "white"
                    }
                    Text {
                        id: visibleZones
                        text: statisticsView.visibleZoneCount
                        color: "white"
                    }

                    Text {
                        text: "|"
                        color: "#666666"
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
                            statisticsView.setAccumulationMode(currentIndex)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "|"
                        color: "#666666"
                    }

                    Button {
                        id: limitRangeBtn
                        text: "Limit Range"
                        checkable: true
                        checked: statisticsView.limitRangeActive

                        contentItem: Text {
                            text: limitRangeBtn.text
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: limitRangeBtn.checked ? "#bd94ab"
                                : (limitRangeBtn.hovered ? "#505050" : "#666666")
                            border.color: "#555555"
                            border.width: 1
                        }

                        onCheckedChanged: {
                            statisticsView.limitRangeActive = checked
                            statisticsView.model.refreshData()
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
                        text: "Name"
                        color: "white"
                    }

                    TextField {
                        id: filterEdit
                        Layout.fillWidth: true
                        placeholderText: "Enter filter text..."
                        text: statisticsView.filterText
                        color: "white"

                        background: Rectangle {
                            color: "#404040"
                            border.color: "#555555"
                            border.width: 1
                        }

                        onTextChanged: {
                            statisticsView.filterText = text
                        }
                    }

                    Button {
                        text: "clear"
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
                            statisticsView.clearFilter()
                        }
                    }
                }
            }

            /////*fps chart tabs view*/////
            Rectangle {
                id: fpsChartTabsContainer
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                color: "#2D2D2D"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: "#343131"

                        RowLayout {
                            anchors.fill: parent
                            spacing: 0

                            Rectangle {
                                id: lineChartTab
                                Layout.preferredWidth: 120
                                Layout.fillHeight: true
                                color: chartTabBar.currentIndex == 0 ? "#4D4D4D" : "#343131"
                                border.color: chartTabBar.currentIndex == 0 ? "#666666" : "#343131"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "FPS Line Chart"
                                    font.pixelSize: 12
                                    color: "white"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        chartTabBar.currentIndex = 0
                                    }
                                }
                            }

                            Rectangle {
                                id: barChartTab
                                Layout.preferredWidth: 120
                                Layout.fillHeight: true
                                color: chartTabBar.currentIndex == 1 ? "#4D4D4D" : "#343131"
                                border.color: chartTabBar.currentIndex == 1 ? "#666666" : "#343131"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "FPS Bar Chart"
                                    font.pixelSize: 12
                                    color: "white"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        chartTabBar.currentIndex = 1
                                    }
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                            }
                        }
                    }

                    Item {
                        id: chartTabBar
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        property int currentIndex: 0

                        Rectangle {
                            id: lineChartView
                            anchors.fill: parent
                            visible: chartTabBar.currentIndex == 0
                            color: "#343131"

                            FpsChart {
                                id: fpsChartItem
                                anchors.fill: parent
                                anchors.margins: 10

                                fpsValues: statisticsView.getFpsValues()
                                minFps: statisticsView.getMinFps()
                                maxFps: statisticsView.getMaxFps()
                                avgFps: statisticsView.getAvgFps()
                            }
                        }

                        Rectangle {
                            id: barChartView
                            anchors.fill: parent
                            visible: chartTabBar.currentIndex == 1
                            color: "#343131"

                            FpsBarChart {
                                id: fpsBarChartItem
                                anchors.fill: parent
                                anchors.margins: 10

                                fpsValues: statisticsView.getFpsValues()
                                minFps: statisticsView.getMinFps()
                                maxFps: statisticsView.getMaxFps()
                                avgFps: statisticsView.getAvgFps()
                            }
                        }
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
                    spacing: 0

                    Rectangle {
                        id: nameHeader
                        width: nameColumnWidth
                        height: 30
                        color: "#2D2D2D"

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

                                statisticsView.sort(
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
                                        var newLocationWidth = Math.max(60, startLocationWidth - delta);

                                        if (newNameWidth + newLocationWidth <= availableWidth) {
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

                                statisticsView.sort(
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

                        property bool isSorted: false
                        property bool sortAscending: true

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
                                    totalTimeHeader.sortAscending = true;
                                }

                                statisticsView.sort(
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
                                    countHeader.sortAscending = true;
                                }

                                statisticsView.sort(
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

                        property bool isSorted: false
                        property bool sortAscending: true

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "Mtpc"
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
                                    mtpcHeader.sortAscending = true;
                                }

                                statisticsView.sort(
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
                                    threadsHeader.sortAscending = true;
                                }

                                statisticsView.sort(
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
                    model: statisticsView ? statisticsView.model : null
                    spacing: 1
                    clip: true

                    delegate: Row {
                        id: rowDelegate
                        spacing: 0

                        Rectangle {
                            width: nameColumnWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            clip: true

                            Text {
                                id: nameText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                width: parent.width - 16
                                clip: true
                                text: model.Name
                                color: "white"
                                horizontalAlignment: Text.AlignLeft
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true

                                onDoubleClicked: {
                                    statisticsView.openSource(index)
                                }
                            }
                        }

                        Rectangle {
                            width: locationColumnWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            clip: true


                            Text {
                                id: locationText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                width: parent.width - 16
                                clip: true
                                text: Location
                                color: "white"
                                horizontalAlignment: Text.AlignLeft
                            }
                        }


                        Rectangle {
                            width: totalTimeWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            clip: true

                            Text {
                                anchors.centerIn: parent
                                text: Totaltime
                                clip: true
                                color: "white"
                            }
                        }

                        Rectangle {
                            width: countWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            clip: true

                            Text {
                                anchors.centerIn: parent
                                text: Count
                                clip: true
                                color: "white"
                            }
                        }

                        Rectangle {
                            width: mtpcWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            clip: true

                            Text {
                                anchors.centerIn: parent
                                text: Mtpc
                                clip: true
                                color: "white"
                            }
                        }

                        Rectangle {
                            width: threadsWidth
                            height: 36
                            color: index % 2 === 0 ? "#2D2D2D" : "#3F3F3F"
                            clip: true

                            Text {
                                anchors.centerIn: parent
                                text: Threadcount
                                clip: true
                                color: "white"
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: statisticsView
        function onZoneCountChanged() {
            totalZoneCount.text = statisticsView.totalZoneCount
            visibleZones.text = statisticsView.visibleZoneCount
        }
        function onLimitRangeActiveChanged() {
            limitRangeBtn.checked = statisticsView.limitRangeActive
        }
        function onFilterTextChanged() {
            filterEdit.text = statisticsView.filterText
        }

        function onFpsDataChanged() {
            fpsChartItem.fpsValues = statisticsView.getFpsValues()
            fpsChartItem.minFps = statisticsView.getMinFps()
            fpsChartItem.maxFps = statisticsView.getMaxFps()
            fpsChartItem.avgFps = statisticsView.getAvgFps()
            fpsChartItem.update()

            fpsBarChartItem.fpsValues = statisticsView.getFpsValues()
            fpsBarChartItem.minFps = statisticsView.getMinFps()
            fpsBarChartItem.maxFps = statisticsView.getMaxFps()
            fpsBarChartItem.avgFps = statisticsView.getAvgFps()
            fpsBarChartItem.update()
        }


    }

    Connections {
        target: statisticsView.model
        function onStatisticsUpdated() {
            listView.forceLayout()
        }

    }


}