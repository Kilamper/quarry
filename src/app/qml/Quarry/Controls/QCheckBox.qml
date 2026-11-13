pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding, implicitIndicatorHeight + topPadding + bottomPadding, Theme.controlHeight)
    spacing: Theme.spacing * 2
    padding: Theme.spacing
    font.pointSize: Theme.fontSize
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.6

    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.focusable: true
    Accessible.checkable: true
    Accessible.checked: checkState === Qt.Checked
    Accessible.checkStateMixed: checkState === Qt.PartiallyChecked
    Accessible.onToggleAction: control.toggle()

    indicator: Rectangle {
        implicitWidth: Math.round(Theme.controlHeight * 0.7)
        implicitHeight: implicitWidth
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: Math.round(Theme.radius / 2)
        color: control.checkState !== Qt.Unchecked ? Theme.accent : Theme.bgSurface
        border.width: control.down ? 2 : 1
        border.color: !control.enabled ? Theme.borderDefault : control.checkState !== Qt.Unchecked ? Theme.accentHover : Theme.borderStrong

        // The mark (check / minus) carries the state, not only the fill color.
        Icon {
            anchors.centerIn: parent
            visible: control.checkState !== Qt.Unchecked
            text: control.checkState === Qt.PartiallyChecked ? Icons.minus : Icons.check
            color: Theme.fgOnAccent
        }

        FocusRing {
            shown: control.visualFocus
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? Theme.fgDefault : Theme.fgDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
