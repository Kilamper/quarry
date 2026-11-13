pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Left/Right arrows move between tabs (handled by the template).
T.TabBar {
    id: control

    property string accessibleName: ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, contentHeight + topPadding + bottomPadding, Theme.controlHeight)
    spacing: 0

    Accessible.role: Accessible.PageTabList
    Accessible.name: accessibleName

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
        highlightMoveDuration: 0
    }

    background: Rectangle {
        color: Theme.bgSurface

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.borderDefault
        }
    }
}
