pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

T.MenuItem {
    id: control

    // Already formatted shortcut text shown at the right edge, e.g. "Ctrl+T".
    property string shortcutText: ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding, Theme.controlHeight)
    verticalPadding: Theme.spacing
    horizontalPadding: Theme.spacing * 2
    spacing: Theme.spacing * 2
    font.pointSize: Theme.fontSize
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.6

    Accessible.role: Accessible.MenuItem
    Accessible.name: text
    Accessible.description: shortcutText
    Accessible.focusable: true
    Accessible.checkable: checkable
    Accessible.checked: checked
    Accessible.onPressAction: control.triggered()

    indicator: Icon {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.checked
        text: Icons.check
        color: control.enabled ? Theme.fgDefault : Theme.fgDisabled
    }

    arrow: Icon {
        x: control.mirrored ? control.leftPadding : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.subMenu
        text: Icons.chevronRight
        color: control.enabled ? Theme.fgDefault : Theme.fgDisabled
    }

    contentItem: RowLayout {
        spacing: control.spacing

        Text {
            Layout.fillWidth: true
            leftPadding: control.checkable ? control.indicator.width + Theme.spacing : 0
            text: control.text
            font: control.font
            color: control.enabled ? Theme.fgDefault : Theme.fgDisabled
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            visible: control.shortcutText !== ""
            text: control.shortcutText
            font: control.font
            color: control.enabled ? Theme.fgMuted : Theme.fgDisabled
            rightPadding: control.subMenu ? control.arrow.width : 0
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.enabled && (control.highlighted || control.hovered) ? Theme.bgHover : Qt.alpha(Theme.bgRaised, 0)
        border.width: control.highlighted && control.enabled ? 1 : 0
        border.color: Theme.focusRing
    }
}
