pragma ComponentBehavior: Bound
import QtQuick

// Icon-only button. accessibleName is required: it is the accessible name and the tooltip text,
// to which the optional shortcut (already formatted, e.g. "Ctrl+K") is appended.
QButton {
    id: control

    required property string accessibleName
    property string shortcutText: ""
    property bool tipDismissed: false
    readonly property alias tooltip: tip

    variant: "flat"
    Accessible.name: accessibleName
    Accessible.description: shortcutText

    onHoveredChanged: tipDismissed = false
    onVisualFocusChanged: tipDismissed = false
    Keys.onEscapePressed: event => {
        // Esc dismisses the tooltip first (WCAG 1.4.13); otherwise it is not ours.
        event.accepted = tip.visible;
        tipDismissed = true;
    }

    QToolTip {
        id: tip

        visible: (control.hovered || control.visualFocus) && !control.tipDismissed
        delay: 500
        text: control.shortcutText !== "" ? control.accessibleName + " (" + control.shortcutText + ")" : control.accessibleName
    }
}
