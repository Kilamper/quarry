pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

T.TextField {
    id: control

    // Accessible name; falls back to the placeholder. Give every field a real label.
    property string accessibleName: ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, contentWidth + leftPadding + rightPadding, Theme.controlHeight * 6)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, contentHeight + topPadding + bottomPadding, Theme.controlHeight)
    topPadding: Theme.spacing
    bottomPadding: Theme.spacing
    leftPadding: Theme.spacing * 2
    rightPadding: Theme.spacing * 2
    font.pointSize: Theme.fontSize
    color: enabled ? Theme.fgDefault : Theme.fgDisabled
    placeholderTextColor: Theme.fgMuted
    selectionColor: Theme.selectionBg
    selectedTextColor: Theme.selectionFg
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.6

    Accessible.role: Accessible.EditableText
    Accessible.name: accessibleName !== "" ? accessibleName : placeholderText
    Accessible.editable: !readOnly
    Accessible.readOnly: readOnly
    Accessible.passwordEdit: echoMode === TextInput.Password
    Accessible.focusable: true

    background: Rectangle {
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.enabled ? Theme.bgSurface : Theme.bgCanvas
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.focusRing : control.enabled ? Theme.borderStrong : Theme.borderDefault
    }

    FocusRing {
        shown: control.activeFocus
    }
}
