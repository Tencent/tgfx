import QtQuick 2.9
import com.kdab.dockwidgets 2.0

Rectangle {
    id: root

    property GroupView groupCpp
    readonly property QtObject titleBarCpp: groupCpp ? groupCpp.titleBar : null
    readonly property int nonContentsHeight: (titleBar.item ? titleBar.item.heightWhenVisible : 0) + tabbar.implicitHeight + (2 * contentsMargin) + titleBarContentsMargin // qmllint disable missing-property
    property int contentsMargin: 2
    property int titleBarContentsMargin: 2
    property int mouseResizeMargin: 8
    readonly property bool isMDI: groupCpp && groupCpp.isMDI
    readonly property bool resizeAllowed: root.isMDI && !KDDockWidgets.Singletons.helpers.isDragging && KDDockWidgets.Singletons.dockRegistry && (!KDDockWidgets.Singletons.helpers.groupViewInMDIResize || KDDockWidgets.Singletons.helpers.groupViewInMDIResize === groupCpp)
    property alias tabBarHeight: tabbar.height
    readonly property bool hasCustomMouseEventRedirector: false
    readonly property bool isFixedHeight: groupCpp && groupCpp.isFixedHeight
    readonly property bool isFixedWidth: groupCpp && groupCpp.isFixedWidth
    readonly property bool tabsAtTop: !groupCpp || !groupCpp.tabsAtBottom

    anchors.fill: parent

    radius: 0
    color: "transparent"
    border {
        color: "#383838"
        width: 4
    }

    onGroupCppChanged: {
        if (groupCpp) {
            groupCpp.setStackLayout(stackLayout);
        }
    }

    onNonContentsHeightChanged: {
        if (groupCpp)
            groupCpp.geometryUpdated();
    }



    Loader {
        id: titleBar
        readonly property QtObject titleBarCpp: root.titleBarCpp
        source: "qrc:/qml/TitleBar.qml"

        anchors {
            top: parent ? parent.top : undefined
            left: parent ? parent.left : undefined
            right: parent ? parent.right : undefined
            topMargin: root.titleBarContentsMargin
            leftMargin: root.titleBarContentsMargin
            rightMargin: root.titleBarContentsMargin
        }
    }

    Loader {
        id: tabbar
        readonly property GroupView groupCpp: root.groupCpp
        readonly property bool hasCustomMouseEventRedirector: root.hasCustomMouseEventRedirector

        source: "qrc:/qml/TabBar.qml"

        function topAnchor() {
            if (root.tabsAtTop) {
                return (titleBar && titleBar.visible) ? titleBar.bottom : (parent ? parent.top : undefined);
            } else {
                return undefined;
            }
        }

        anchors {
            left: parent ? parent.left : undefined
            right: parent ? parent.right : undefined
            top: topAnchor()
            bottom: root.tabsAtTop ? undefined : parent.bottom

            // 1 pixel gap so we don't overlap with outer frame. We shouldn't hardcode this though
            leftMargin: 1
            rightMargin: 1
        }
    }

    Item {
        id: stackLayout

        function bottomAnchor() {
            if (!parent)
                return undefined;

            if (root.tabsAtTop || !tabbar.visible)
                return parent.bottom;

            return tabbar.top;
        }

        anchors {
            left: parent ? parent.left : undefined
            right: parent ? parent.right : undefined
            top: (parent && tabbar.visible && root.tabsAtTop) ? tabbar.bottom : ((titleBar && titleBar.visible) ? titleBar.bottom : parent ? parent.top : undefined)
            bottom: bottomAnchor()

            leftMargin: root.contentsMargin
            rightMargin: root.contentsMargin
            bottomMargin: root.contentsMargin
        }
    }
}
