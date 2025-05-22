import QtQuick 2.6
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Window 2.12
import com.kdab.dockwidgets 2.0 as KDDW


ApplicationWindow {
    id: startView
    width: 1000
    height: 600
    visible: true
    title: "Inspector-welcome"
    color: "#333333"
    property int selectedIndex: -1


    ///*function*///
    Column {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        Rectangle {
            width: parent.width
            height: 30
            color: "#535353"
            border.color: "black"
            border.width: 1

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: "Open File"
                color: "white"
                font.pixelSize: 14
            }
        }

        ///* recent file and drag file open*///
        Row {
            width: parent.width
            height: 200
            spacing: 1

            //todo: use a listView or another view to show the recent file, need to consider the C++
            Rectangle {
                width: parent.width / 2
                height: parent.height
                color: "#2d2d2d"

                Text {
                    id: recentFilesTitle
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.topMargin: 5
                    text: "Recent File"
                    color: "white"
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
                        color: mouseArea.containsMouse ? "#6b6b6b" : "transparent"
                        radius: 3

                        Column {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: modelData.filesName
                                color: "white"
                                font.pixelSize: 12
                                width: parent.width
                                elide: Text.ElideMiddle
                            }

                            Text {
                                text: modelData.filesPath
                                color: "#aaaaaa"
                                font.pixelSize: 10
                                width: parent.width
                                elide: Text.ElideMiddle
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onDoubleClicked: {
                                if (startViewModel) {
                                    startViewModel.openFile(modelData.filesPath)
                                }
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        background: Rectangle {
                            color: "#2d2d2d"
                        }
                        contentItem: Rectangle {
                            color: "#535353"
                            implicitWidth: 6
                            radius: 3
                        }
                    }
                }
            }

            Rectangle {
                width: 1
                height: parent.height
                color: "black"
                visible: true
            }

            //todo: define a drop area to openfile, need to consider the C++
            Rectangle {
                width: parent.width / 2
                height: parent.height
                color: "#2d2d2d"

                Text {
                    id: dragAreaTitle
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.topMargin: 5
                    text: "Drag Open File"
                    color: "white"
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
                    color: dropArea.containsMouse ? "#3d3d3d" : "#2d2d2d"
                    border.color: dropArea.containsDrag ? "#666666" : "#444444"
                    border.width: 2
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
                            dropContainer.color = "#2d2d2d"
                        }
                    }
                }
            }
        }

        ///*seperator*///
        Rectangle {
            width: parent.width
            height: 1
            color: "#383838"
        }

        Rectangle {
            width: parent.width
            height: 30
            color: "#535353"
            border.color: "black"
            border.width: 1

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: "Start Connection"
                color: "white"
                font.pixelSize: 14
            }
        }

        Row {
            width: parent.width
            height: 200
            spacing: 1

            Rectangle {
                width: parent.width / 2
                height: parent.height
                color: "#2d2d2d"

                Text {
                    id: clientsTitle
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.topMargin: 5
                    text: "Clients"
                    color: "white"
                    font.pixelSize: 14
                }

                ListView {
                    id: clientsList
                    anchors.top: clientsTitle.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    spacing: 6
                    clip: true
                    model: startViewModel ? startViewModel.clientItems : null

                    delegate: Rectangle {
                        width: clientsList.width
                        height: 40
                        color: mouseArea.containsMouse ? "#6b6b6b" : "transparent"
                        radius: 3

                        Column {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: modelData.procName
                                color: "white"
                                font.pixelSize: 12
                                width: parent.width
                                elide: Text.ElideMiddle
                            }

                            Text {
                                text: modelData.caddress + ":" + modelData.port
                                color: "#aaaaaa"
                                font.pixelSize: 10
                                width: parent.width
                                elide: Text.ElideMiddle
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onDoubleClicked: {
                                if (startViewModel && modelData) {
                                    startViewModel.connectToClient(modelData)
                                }
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        background: Rectangle {
                            color: "#2d2d2d"
                        }
                        contentItem: Rectangle {
                            color: "#535353"
                            implicitWidth: 6
                            radius: 3
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: clientsList.count === 0 ? "Waiting for project start" : ""
                    color: "#44ffffff"
                    font.pixelSize: 14
                }
            }

            //seperator
            Rectangle {
                width: 1
                height: parent.height
                color: "black"
                visible: true
            }

            Rectangle {
                width: parent.width / 2
                height: parent.height
                color: "#2d2d2d"


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
                                color: "white"
                                font.pixelSize: 14
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        MouseArea {
                            //id: frameCaptureArea
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
                                color: "white"
                                font.pixelSize: 14
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        MouseArea {
                            //id: layerTreeArea
                            anchors.fill: parent
                            onClicked: selectedIndex = 1
                        }
                    }
                }
            }
        }
    }

    ///* cancel and launch button*///
    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        spacing: 10

        Rectangle {
            id: cancelBtn
            width: 100
            height: 30
            radius: 3
            color: cancelArea.containsMouse ? "#535353" : "#444444"

            Text {
                anchors.centerIn: parent
                text: "Cancel"
                color: "white"
                font.pixelSize: 14
            }

            MouseArea {
                id: cancelArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    Qt.quit();
                }
            }
        }

        Rectangle {
            id: launchBtn
            width: 100
            height: 30
            radius: 3
            color: launchArea.containsMouse ? "#535353" : "#444444"


            Text {
                anchors.centerIn: parent
                text: "Launch"
                color: "white"
                font.pixelSize: 14
            }


            MouseArea {
                id: launchArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    //todo: launch the main view
                }
            }
        }
    }


    Connections {
        target: startViewModel
        function onCloseWindow() {
            startView.close();
        }
    }
}
