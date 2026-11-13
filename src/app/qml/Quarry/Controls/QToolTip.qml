pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

T.ToolTip {
    id: control

    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: parent ? parent.height + Theme.spacing : 0
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    margins: Theme.spacing
    verticalPadding: Theme.spacing
    horizontalPadding: Theme.spacing * 2
    font.pointSize: Theme.fontSize
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.animationDuration
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.animationDuration
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.fgDefault
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        color: Theme.bgRaised
        radius: Theme.radius
        border.width: 1
        border.color: Theme.borderStrong
    }
}
