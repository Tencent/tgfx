import QtQuick
import QtQuick.Controls
import com.kdab.dockwidgets 2.0 as KDDW
//import TextureDrawer 1.0

Item {
    id: textureView
    width: 1120
    height: 800
    property var window: Window.window

    KDDW.DockingArea {
        id: root
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            top: parent.top
        }

        persistentCentralItemFileName: "qrc:/qml/TextureDrawer.qml"
        options: KDDW.KDDockWidgets.MainWindowOption_HasCentralWidget

        uniqueName: "TextureDrawer"

        KDDW.DockWidget {
            id: inputDock
            uniqueName: "inputDock"
            Rectangle {
                id: guest
                color: "#2E8BC0"
                anchors.fill: parent
            }
        }

        KDDW.DockWidget {
            id: outputDock
            uniqueName: "outputDock"
            Rectangle {
                color: "black"
                anchors.fill: parent
            }
        }

        Component.onCompleted: {
            addDockWidget(inputDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(200, 0));
            inputDock.addDockWidgetAsTab(outputDock);
        }
    }
}
