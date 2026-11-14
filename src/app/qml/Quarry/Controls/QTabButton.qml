pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Tab. With closable: true a close button is shown (accessible name "Close <tab>"), and Delete
// on the focused tab also emits closeRequested(). The owner removes the tab.
T.TabButton {
    id: control

    property bool closable: false
    // Optional decoration before the title (a color dot) and after it (a badge). Decorative: the
    // owner puts what they say into Accessible.name.
    property alias leading: leadingRow.data
    property alias trailing: trailingRow.data

    signal closeRequested

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding, Theme.controlHeight)
    verticalPadding: 0
    leftPadding: Theme.spacing * 2
    rightPadding: closable ? Theme.spacing : Theme.spacing * 2
    font.pointSize: Theme.fontSize
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.focusable: true
    Accessible.selectable: true
    Accessible.selected: checked
    Accessible.onPressAction: control.click()

    Keys.onDeletePressed: event => {
        event.accepted = control.closable;
        if (control.closable)
            control.closeRequested();
    }

    contentItem: Row {
        spacing: Theme.spacing

        Row {
            id: leadingRow

            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.checked ? Theme.fgDefault : Theme.fgMuted
            elide: Text.ElideRight
        }

        Row {
            id: trailingRow

            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.closable
            accessibleName: qsTr("Close %1").arg(control.text)
            iconGlyph: Icons.x
            focusPolicy: Qt.NoFocus
            onClicked: control.closeRequested()
        }
    }

    background: Rectangle {
        color: control.checked ? Theme.bgCanvas : control.hovered ? Theme.bgHover : Theme.bgSurface

        // The selected tab is marked with an accent bar, not only a fill.
        Rectangle {
            visible: control.checked
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.focusRingWidth
            color: Theme.accent
        }
    }

    FocusRing {
        shown: control.visualFocus
        inset: true
    }
}
