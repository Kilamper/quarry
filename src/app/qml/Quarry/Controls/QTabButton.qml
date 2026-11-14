pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
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

    // One line: decoration, title, badge, close. When the tab is too narrow for that (200% zoom,
    // long names) the badge drops to a second line instead of being pushed out of the tab.
    contentItem: GridLayout {
        readonly property bool fits: control.availableWidth >= leadingRow.implicitWidth + titleText.implicitWidth + trailingRow.implicitWidth + closeButton.implicitWidth + columnSpacing * 3

        columns: 4
        columnSpacing: Theme.spacing
        rowSpacing: 0

        RowLayout {
            id: leadingRow

            Layout.row: 0
            Layout.column: 0
            Layout.alignment: Qt.AlignVCenter
            visible: implicitWidth > 0
            spacing: Theme.spacing
        }

        Text {
            id: titleText

            Layout.row: 0
            Layout.column: 1
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            text: control.text
            font: control.font
            color: control.checked ? Theme.fgDefault : Theme.fgMuted
            elide: Text.ElideRight
        }

        RowLayout {
            id: trailingRow

            Layout.row: parent.fits ? 0 : 1
            Layout.column: parent.fits ? 2 : 0
            Layout.columnSpan: parent.fits ? 1 : 4
            Layout.alignment: Qt.AlignVCenter
            visible: implicitWidth > 0
            spacing: Theme.spacing
        }

        IconButton {
            id: closeButton

            Layout.row: 0
            Layout.column: 3
            Layout.alignment: Qt.AlignVCenter
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
