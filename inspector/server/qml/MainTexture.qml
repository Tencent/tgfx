import QtQuick 2.9
import QtQuick.Window 2.13
import FramesDrawer 1.0
import TextureDrawer 1.0
Item {
    id: wind
    visible: true

    ///* use to create a module which named TextureDrawer *///
    // FramesDrawer {
    //     id: frameView
    //     worker: workerPtr
    //     viewData: viewDataPtr
    //     anchors.left: parent.left
    //     anchors.top: parent.top
    //     anchors.bottom: parent.bottom
    //     anchors.right: parent.right
    //     objectName: "framesView"
    // }
    Image {
        id: logo
        source: "qrc:icons/InspectorIcon.png"
        fillMode: Image.PreserveAspectFit
        anchors {
            fill: parent
            margins: 150
        }
    }
    // TextureDrawer {
    //     //anchors.centerIn: parent
    //     id:mainTexture
    //     width:wind.width
    //     height:wind.height
    //     source: "/Users/junhaoduan/Desktop/Dev/tgfx/resources/assets/bridge.jpg"
    // }
}
