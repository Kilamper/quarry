pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// variant: "default" | "primary" | "danger" | "flat". Danger always shows its text and a warning
// icon, so it never relies on red alone. Hover/press also thicken the border (visible in
// high contrast, where fills may be identical).
T.Button {
    id: control

    property string variant: "default"
    property string iconGlyph: ""
    readonly property string effectiveGlyph: iconGlyph !== "" ? iconGlyph : (variant === "danger" ? Icons.triangleAlert : "")
    readonly property color foreground: !enabled ? Theme.fgDisabled
        : variant === "primary" ? Theme.fgOnAccent
        : variant === "danger" ? Theme.danger
        : Theme.fgDefault

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding, Theme.controlHeight)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding, Theme.controlHeight)
    verticalPadding: Theme.spacing
    horizontalPadding: Theme.spacing * 2
    spacing: Theme.spacing
    font.pointSize: Theme.fontSize
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.6

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.focusable: true
    Accessible.checkable: checkable
    Accessible.checked: checked
    Accessible.pressed: down
    Accessible.onPressAction: control.click()

    Keys.onReturnPressed: control.click()
    Keys.onEnterPressed: control.click()

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row

            anchors.centerIn: parent
            spacing: control.spacing

            Icon {
                visible: control.effectiveGlyph !== ""
                text: control.effectiveGlyph
                color: control.foreground
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                visible: text !== ""
                text: control.text
                font: control.font
                color: control.foreground
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        readonly property bool active: control.enabled && (control.down || control.hovered)

        implicitWidth: Theme.controlHeight
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: !control.enabled ? (control.variant === "flat" ? Qt.alpha(Theme.bgSurface, 0) : Theme.bgCanvas)
            : control.variant === "primary" ? (active ? Theme.accentHover : Theme.accent)
            : control.down ? Theme.bgPressed
            : control.hovered ? Theme.bgHover
            : control.variant === "flat" ? Qt.alpha(Theme.bgSurface, 0)
            : Theme.bgRaised
        border.width: !control.enabled ? 1 : active ? 2 : (control.variant === "flat" ? 0 : 1)
        border.color: !control.enabled ? Theme.borderDefault
            : control.variant === "danger" ? Theme.danger
            : control.variant === "primary" ? Theme.accentHover
            : Theme.borderStrong

        Behavior on color {
            ColorAnimation {
                duration: Theme.animationDuration
            }
        }
    }

    FocusRing {
        shown: control.visualFocus
    }
}
