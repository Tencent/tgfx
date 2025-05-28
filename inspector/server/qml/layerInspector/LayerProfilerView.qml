import QtQuick 2.6
import QtQuick.Controls 2.12
import com.kdab.dockwidgets 2.0 as KDDW

ApplicationWindow {
    id: layerProfilerView
    visible: true
    width: 1920
    height: 1080
    color: "#2d2d2d"
    title : "LayerProfiler"

    menuBar: MenuBar{
        Menu {
            title: qsTr("&LayerProfiler")
            Action {
                text: qsTr("About LayerProfiler")
                onTriggered: {
                    console.log("About LayerProfiler clicked")
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
                text: qsTr("DisConnect")
                shortcut: "Ctrl+Shift+D"
                onTriggered: {
                    console.log("DisConnect clicked")
                    //todo: disconnect the client
                }
            }
            MenuSeparator {}
            Action {
                text: qsTr("Close")
                shortcut: StandardKey.Close
                onTriggered: {
                    layerProfilerView.hide()
                    _layerProfileView.openStartView()
                }
            }
        }

        Menu{
            title: "Window"

            Action {
                text: qsTr("Save Layout")
                shortcut: "Ctrl+Shift+L"
                onTriggered: {
                    layoutSaver.saveToFile("layerInspectorLayout.json")
                }
            }

            Action {
                text: qsTr("Reload Layout")
                shortcut: "Ctrl+Shift+S"
                onTriggered: {
                    layoutSaver.restoreFromFile("layerInspectorLayout.json")
                }
            }

            Action {
                text: qsTr("Close All")
                onTriggered: {
                    _kddwDockRegistry.clear();
                }
            }
        }
    }

    KDDW.DockingArea {
        anchors.fill: parent
        uniqueName: "LayerProfilerLayout"

        KDDW.DockWidget{
            id: layerTree
            uniqueName: "RenderTree" // Each dock widget needs a unique id
            source: "qrc:/qml/layerInspector/LayerTree.qml"

        }

        KDDW.DockWidget{
            id: layerAttributeTree
            uniqueName: "Attribute" // Each dock widget needs a unique id
            source: "qrc:/qml/layerInspector/LayerAttribute.qml"
        }

        Component.onCompleted: {
            addDockWidget(layerAttributeTree, KDDW.KDDockWidgets.Location_OnRight);

            // Add dock5 to the left of dock4
            addDockWidget(layerTree, KDDW.KDDockWidgets.Location_OnLeft);
        }
    }

    KDDW.LayoutSaver {
        id: layoutSaver
    }
}