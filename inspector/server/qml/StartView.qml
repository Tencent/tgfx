import QtQuick 2.6
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Window 2.12
import QtQuick.Dialogs
import com.kdab.dockwidgets 2.0 as KDDW
import Qt5Compat.GraphicalEffects


ApplicationWindow {
    id: startView
    width: 800
    height: 600
    minimumWidth: 400
    minimumHeight: 300
    maximumWidth: 1600
    maximumHeight: 1200
    visible: true
    title: "Inspector-welcome"
    color: "#383838"
    property int selectedIndex: 0
    property int selectedFilePathIndex: -1
    property int selectedClientIndex: -1

    FileDialog {
        id: openFileDialog
        title: qsTr("open File")
        fileMode: FileDialog.OpenFile
        nameFilters: [ "Inspector files (*.isp)" ]
        onAccepted: {
            startViewModel.openFile(openFileDialog.selectedFile)
            close()
        }
    }

    Column {
        anchors.fill: parent
        spacing: 2
        clip: true
        ///* open file area header*///
        Column {
            spacing: 2
            width: parent.width

            Rectangle {
                width: parent.width
                height: 30
                color: "#535353"

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    spacing: 10
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Open File"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                    }

                    Item {
                        height: 30
                        width: 30
                        Image {
                            id: openFile
                            source: "qrc:icons/chooseOpenFile.png"
                            width: 20
                            height: 20
                            anchors.centerIn: parent

                            ColorOverlay{
                                anchors.fill: parent
                                color: "#DDDDDD"
                                source: parent
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: parent.scale = 1.1
                            onExited: parent.scale = 1.0
                            onClicked: {
                                openFileDialog.open();
                            }
                        }

                    }
                }
            }
        }

        ///* open file and drag open file area *///
        Row {
            width: parent.width
            height: (parent.height - 110) * 0.45
            spacing: 2

            Rectangle {
                width: parent.width / 2
                height: parent.height
                color: "#434343"

                Text {
                    id: recentFilesTitle
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.topMargin: 5
                    text: "Recent File"
                    color: "#DDDDDD"
                    font.pixelSize: 14
                }

                ListView {
                    id: recentFilesList
                    anchors.top: recentFilesTitle.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    spacing: 6
                    clip: true
                    model: startViewModel ? startViewModel.fileItems : null

                    delegate: Rectangle {
                        width: recentFilesList.width
                        height: 40
                        color: {
                            if(mouseArea.containsMouse) {
                                return "#6b6b6b"
                            }
                            else if(index === selectedFilePathIndex) {
                                return "#6b6b6b"
                            }
                            else {
                                return "transparent"
                            }
                        }
                        radius: 3

                        Column {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: modelData.filesName
                                color: "#DDDDDD"
                                font.pixelSize: 12
                                width: parent.width
                                elide: Text.ElideMiddle
                            }

                            Text {
                                text: modelData.filesPath
                                color: "#dddddd"
                                font.pixelSize: 10
                                width: parent.width
                                elide: Text.ElideMiddle
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                selectedFilePathIndex = index
                            }
                            onDoubleClicked: {
                                if (startViewModel && selectedIndex === 0) {
                                    startViewModel.openFile(modelData.filesPath)
                                }
                                else if(selectedIndex === 1) {
                                    console.error("Error: Cannot open file by double-clicking in LayerTree mode")
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width / 2
                height: parent.height
                color: "#434343"

                Text {
                    id: dragAreaTitle
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.topMargin: 5
                    text: "Drag Open File"
                    color: "#DDDDDD"
                    font.pixelSize: 14
                }

                Rectangle {
                    id: dropContainer
                    anchors.top: dragAreaTitle.bottom
                    anchors.topMargin: 10
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 20
                    color: dropArea.containsMouse ? "#2d2d2d" : "#434343"
                    radius: 5

                    Image {
                        anchors.centerIn: parent
                        source : "qrc:/icons/openfile.png"
                        width: 48
                        height: 48
                        opacity: 0.5
                    }

                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 50
                        text: "Drag file to here"
                        color: "#44ffffff"
                        font.pixelSize: 14
                    }

                    DropArea {
                        id: dropArea
                        anchors.fill: parent

                        onDropped: function(drop) {
                            if (drop.hasUrls) {
                                var fileUrl = drop.urls[0];
                                var filePath;
                                if (Qt.platform.os === "windows") {
                                    filePath = fileUrl.toString().replace(/^(file:\/{3})|^file:\//, "");
                                } else {
                                    filePath = fileUrl.toString().replace(/^(file:\/{2})|^file:\//, "");
                                }
                                filePath = decodeURIComponent(filePath);
                                if (startViewModel) {
                                    startViewModel.openFile(filePath);
                                }
                            }
                        }

                        onEntered: {
                            dropContainer.color = "#3d3d3d"
                        }

                        onExited: {
                            dropContainer.color = "#434343"
                        }
                    }
                }
            }
        }

        ///* open client and select launcher header*///
        Column {
            spacing: 2
            width: parent.width

            Rectangle {
                width: parent.width
                height: 30
                color: "#535353"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Start Connection"
                    color: "#DDDDDD"
                    font.pixelSize: 14
                }
            }
        }

        ///* open client and select launcher area *///
        Row {
            width: parent.width
            height: (parent.height - 110) * 0.55
            spacing: 2

            Rectangle {
                width: parent.width / 2
                height: parent.height
                color: "#434343"

                Text {
                    id: clientsTitle
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.topMargin: 5
                    text: "Clients"
                    color: "#DDDDDD"
                    font.pixelSize: 14
                }

                ListView {
                    id: frameCaptureclientsList
                    visible: selectedIndex === 0
                    anchors.top: clientsTitle.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    spacing: 6
                    clip: true
                    model: startViewModel ? startViewModel.frameCaptureClientItems : null

                    delegate: Rectangle {
                        width: frameCaptureclientsList.width
                        height: 40
                        color: {
                            if(mouseArea.containsMouse) {
                                return "#6b6b6b"
                            }
                            else if(index === selectedClientIndex) {
                                return "#6b6b6b"
                            }
                            else {
                                return "transparent"
                            }
                        }
                        radius: 3

                        Column {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: modelData.procName
                                color: modelData.connected ? "#888888" : "#DDDDDD"
                                font.pixelSize: 12
                                width: parent.width
                                elide: Text.ElideMiddle
                            }

                            Text {
                                text: modelData.address + ":" + modelData.port
                                color: modelData.connected ? "#777777" : "#AAAAAA"
                                font.pixelSize: 10
                                width: parent.width
                                elide: Text.ElideMiddle
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            enabled: !modelData.connected
                            onClicked: {
                                selectedClientIndex = index
                            }

                            onDoubleClicked: {
                                if (startViewModel) {
                                    if(selectedIndex === 0){
                                        startViewModel.connectToClient(modelData)
                                    }else if(selectedIndex === 1){
                                        startViewModel.connectToClientByLayerInspector(modelData)
                                    }
                                }
                                startView.hide()
                            }
                        }
                    }
                }

                ListView {
                    id: layerTreeClientsList
                    visible: selectedIndex === 1
                    anchors.top: clientsTitle.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    spacing: 6
                    clip: true
                    model: startViewModel ? startViewModel.layerTreeClientItems : null

                    delegate: Rectangle {
                        width: layerTreeClientsList.width
                        height: 40
                        color: {
                            if(mouseArea.containsMouse) {
                                return "#6b6b6b"
                            }
                            else if(index === selectedClientIndex) {
                                return "#6b6b6b"
                            }
                            else {
                                return "transparent"
                            }
                        }
                        radius: 3

                        Column {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: modelData.procName
                                color: modelData.connected ? "#888888" : "#DDDDDD"
                                font.pixelSize: 12
                                width: parent.width
                                elide: Text.ElideMiddle
                            }

                            Text {
                                text: modelData.address + ":" + modelData.port
                                color: modelData.connected ? "#777777" : "#AAAAAA"
                                font.pixelSize: 10
                                width: parent.width
                                elide: Text.ElideMiddle
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            enabled: !modelData.connected
                            onClicked: {
                                selectedClientIndex = index
                            }

                            onDoubleClicked: {
                                if (startViewModel) {
                                    if(selectedIndex === 0){
                                        startViewModel.connectToClient(modelData)
                                    }else if(selectedIndex === 1){
                                        startViewModel.connectToClientByLayerInspector(modelData)
                                    }
                                }
                                startView.hide()
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: {
                        if(selectedIndex === 0 && frameCaptureclientsList.count === 0){
                            return "Waitting for project start...";
                        }else if(selectedIndex === 1 && layerTreeClientsList.count === 0){
                            return "Waitting for project start...";
                        }else{
                            return "";
                        }
                    }
                    color: "#44ffffff"
                    font.pixelSize: 14
                }
            }

            Rectangle {
                width: parent.width / 2
                height: parent.height
                color: "#434343"


                Row {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    spacing: 20


                    Rectangle {
                        width: 100
                        height: 120
                        color: selectedIndex === 0 ? "#6b6b6b" : "transparent"
                        radius: 5

                        Column {
                            anchors.centerIn: parent
                            spacing: 5
                            Image {
                                source: "qrc:/icons/InspectorIcon.png"
                                width: 80
                                height: 80
                                fillMode: Image.PreserveAspectFit
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                text: "FrameCapture"
                                color: "#DDDDDD"
                                font.pixelSize: 14
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: selectedIndex = 0
                        }
                    }

                    Rectangle {
                        width: 100
                        height: 120
                        color: selectedIndex === 1 ? "#6b6b6b" : "transparent"
                        radius: 5

                        Column {
                            anchors.centerIn: parent
                            spacing: 5
                            Image {
                                source: "qrc:/icons/LayerTreeIcon.png"
                                width: 80
                                height: 80
                                fillMode: Image.PreserveAspectFit
                            }
                            Text {
                                text: "LayerTree"
                                color: "#DDDDDD"
                                font.pixelSize: 14
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: selectedIndex = 1
                        }
                    }
                }
            }
        }

        ///* cancel and launch button *///
        Rectangle {
            width: parent.width
            height: 40
            color: "#535353"

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 20
                spacing: 10


                Rectangle {
                    id: cancelBtn
                    width: 100
                    height: 30
                    radius: 3
                    color: cancelArea.containsMouse ? "#444444" : "#535353"
                    border.color: "#383838"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                    }

                    MouseArea {
                        id: cancelArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: Qt.quit()
                    }
                }


                Rectangle {
                    id: launchBtn
                    width: 100
                    height: 30
                    radius: 3
                    property bool canLaunch:
                        (selectedFilePathIndex !== -1 && selectedIndex === 0) ||
                        (selectedClientIndex !== -1)
                    color: {
                        if(!canLaunch){
                            return "#666666"
                        }
                        else if(launchArea.containsMouse){
                            return "#444444"
                        }
                        else{
                            return "#535353"
                        }
                    }
                    border.color: canLaunch ? "#383838" : "#666666"
                    border.width: 1
                    opacity: canLaunch ? 1.0 : 0.6

                    Text {
                        anchors.centerIn: parent
                        text: "Launch"
                        color: "#DDDDDD"
                        font.pixelSize: 14
                    }

                    MouseArea {
                        id: launchArea
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: launchBtn.canLaunch
                        onClicked: {
                            if(selectedIndex === 0 && selectedFilePathIndex !== -1){
                                var selectedFilePath = startViewModel.fileItems[selectedFilePathIndex].filesPath
                                startViewModel.openFile(selectedFilePath)
                            }
                            else if(selectedClientIndex !== -1){
                                var selectedClient;
                                if(selectedIndex === 0){
                                    selectedClient = startViewModel.frameCaptureClientItems[selectedClientIndex]
                                    startViewModel.connectToClient(selectedClient)
                                } else if(selectedIndex === 1){
                                    selectedClient = startViewModel.layerTreeClientsList[selectedClientIndex]
                                    startViewModel.connectToClientByLayerInspector(selectedClient)
                                }
                            }
                            startView.hide();
                        }
                    }
                }
            }
        }
    }
}
