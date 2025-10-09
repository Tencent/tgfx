import QtQuick 2.6
import QtQuick.Window 2.15
import QtQuick.Dialogs
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.15
import FramesDrawer 1.0
import com.kdab.dockwidgets 2.0 as KDDW

ApplicationWindow {
    id: inspectorView
    visible: true
    width: 1920
    height: 1080
    title : "TGFX Inspector"
    color: "#2d2d2d"

    FileDialog {
        id: saveDialog
        title: qsTr("save File")
        fileMode: FileDialog.SaveFile
        nameFilters: [ "Inspector files (*.isp)" ]
        onAccepted: {
            inspectorViewModel.saveFileAs(saveDialog.selectedFile)
            close()
        }
    }

    MessageDialog {
        id: exitMessageDialog
        buttons: MessageDialog.Ok
        title: qsTr("Open failed")

        onButtonClicked: {
            inspectorView.close()
        }
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&Inspector")
            Action {
                text: qsTr("About Inspector")
                onTriggered: {
                    console.log("About Inspector clicked")
                }
            }

            Action {
                text: qsTr("Quit")
                onTriggered: {
                    Qt.quit();
                }
            }
        }

        Menu {
            title: qsTr("&File")
            Action {
                text: !inspectorViewModel.isOpenFile ? qsTr("DisConnect") : qsTr("Close")
                shortcut: StandardKey.Close
                onTriggered: {
                    close();
                }
            }

            MenuSeparator {}

            Action {
                text: qsTr("Save File")
                shortcut: StandardKey.Save
                enabled: !inspectorViewModel.isOpenFile
                onTriggered: {
                    if (inspectorViewModel.hasSaveFilePath) {
                        inspectorViewModel.saveFile()
                    }
                    else {
                        saveDialog.open()
                    }
                }
            }

            Action {
                text: qsTr("Save As")
                shortcut: StandardKey.SaveAs
                enabled: !inspectorViewModel.isOpenFile
                onTriggered: {
                    saveDialog.open()
                }
            }
        }

        Menu {
            title: qsTr("Edit")
            Action {
                text: qsTr("Next Frame")
                shortcut: "Ctrl+Right"
                onTriggered: inspectorViewModel.nextFrame()
            }

            Action {
                text: qsTr("Pre Frame")
                shortcut: "Ctrl+Left"
                onTriggered: inspectorViewModel.preFrame()
            }
            MenuSeparator {}

            Action {
                text: qsTr("Capture Frame")
                enabled: !inspectorViewModel.isOpenFile
                shortcut: "F12"
                onTriggered: {
                    console.log("Capture Frame clicked")
                    //todo: capture frame api call in the InspectorView
                }
            }

            Action {
                text: qsTr("Capture 3 Frames")
                enabled: !inspectorViewModel.isOpenFile
                shortcut: "Shift+F12"
                onTriggered: {
                    console.log("Capture 3 Frames clicked")
                    //todo: capture 3 frames api call in the InspectorView
                }
            }
        }

        Menu {
            title: "Window"

            Action {
                text: qsTr("Save Layout")
                shortcut: "Ctrl+Shift+L"
                onTriggered: {
                    layoutSaver.saveToFile("mySavedLayout.json")
                }
            }

            Action {
                text: qsTr("Reload Layout")
                shortcut: "Ctrl+Shift+S"
                onTriggered: {
                    layoutSaver.restoreFromFile("mySavedLayout.json")
                }
            }

            MenuSeparator {}
            Action {
                text: qsTr("Attribute")
                shortcut: "Ctrl+Shift+A"
                onTriggered: {
                    if (dockingArea && attributeDock) {
                        attributeDock.visible = true;
                    }
                }
            }

            Action {
                text: qsTr("Mesh")
                shortcut: "Ctrl+Shift+M"
                onTriggered: {
                    if (dockingArea && attributeDock && meshDock) {
                        meshDock.visible = true;
                        attributeDock.addDockWidgetAsTab(meshDock);
                    }
                }
            }

            Action {
                text: qsTr("Texture")
                shortcut: "Ctrl+Shift+H"
                onTriggered: {
                    if (dockingArea && attributeDock && textureDock) {
                        textureDock.visible = true;
                        attributeDock.addDockWidgetAsTab(textureDock);
                    }
                }
            }

            Action {
                text: qsTr("Frame")
                shortcut: "Ctrl+Shift+F"
                onTriggered: {
                    if (dockingArea && frameDock && attributeDock) {
                        frameDock.visible = true;
                        //todo: if frameDock is already in the dockingArea, then remove it from the dockingArea
                        dockingArea.addDockWidget(frameDock, KDDW.KDDockWidgets.Location_OnLeft, attributeDock);
                    }
                }
            }
        }
    }

    Rectangle {
        id: mainContainer
        anchors.fill: parent
        color: "#383838"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0

            Rectangle {
                id: framesContainer
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                color: "#535353"

                Loader {
                    id: framesViewLoader
                    source: "qrc:/qml/FramesView.qml"
                    anchors.fill: parent
                    onLoaded: {
                        item.objectName = "framesDrawer"
                    }
                }
            }

            Item {
                width: parent.width
                height: 2
            }

            Rectangle {
                id: iconToolbar
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                color: "#535353"
                Row {
                    id: iconRow
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 10
                    spacing: 20
                    Item {
                        height: 32
                        width: 32
                        Image {
                            id: disconnectIcon
                            source: "qrc:icons/disconnected.png"
                            width: 32
                            height: 32
                            anchors.centerIn: parent
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: parent.scale = 1.1
                            onExited: parent.scale = 1.0
                            onClicked: {
                                close();
                            }
                        }
                    }

                    Item {
                        height: 32
                        width: 32

                        Image {
                            id: saveFileIcon
                            source: "qrc:icons/savefile.png"
                            width: 32
                            height: 32
                            anchors.centerIn: parent
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: parent.scale = 1.1
                            onExited: parent.scale = 1.0
                            onClicked: {
                                saveDialog.open()
                            }
                        }
                    }

                    Item {
                        height: 32
                        width: 32

                        Image {
                            id: openFileIcon
                            source: "qrc:icons/capture.png"
                            width: 32
                            height: 32
                            anchors.centerIn: parent
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: parent.scale = 1.1
                            onExited: parent.scale = 1.0
                            onClicked: {
                                console.log("capture clicked")
                            }
                        }
                    }
                }
            }

            KDDW.DockingArea {
                id: dockingArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                uniqueName: "MainInspectorLayout"

                KDDW.DockWidget {
                    id: taskDock
                    uniqueName: "Task"
                    source: "qrc:/qml/TaskTreeView.qml"
                }

                KDDW.DockWidget {
                    id: attributeDock
                    uniqueName: "Attribute"
                    source: "qrc:/qml/AttributeView.qml"
                }

                KDDW.DockWidget {
                    id: textureDock
                    uniqueName: "Texture"
                    source: "qrc:/qml/TextureView.qml"
                    visible: false
                }

                Component.onCompleted: {
                    addDockWidget(attributeDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(1500, 0));
                    attributeDock.addDockWidgetAsTab(textureDock);
                    addDockWidget(taskDock, KDDW.KDDockWidgets.Location_OnLeft, attributeDock, Qt.size(420, 0));
                }
            }
        }
    }

    KDDW.LayoutSaver {
        id: layoutSaver
    }

    Connections {
        target: inspectorViewModel
        function onFailedOpenInspectorView(msg) {
            exitMessageDialog.text = msg
            exitMessageDialog.open()
        }
    }
}




