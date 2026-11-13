pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// An error in plain language: icon and title (never color alone), the message, what to do, and
// the driver text in a collapsed "Technical details" section that can be selected and copied.
ColumnLayout {
    id: root

    property string title: ""
    property string message: ""
    property string hint: ""
    property string detail: ""
    property bool expanded: false
    readonly property alias detailsButton: toggle
    readonly property alias detailsText: detailText

    spacing: Theme.spacing

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Icon {
            Layout.alignment: Qt.AlignTop
            text: Icons.circleAlert
            color: Theme.danger
        }

        Text {
            Layout.fillWidth: true
            text: root.title
            font.pointSize: Theme.fontSize
            font.bold: true
            color: Theme.danger
            wrapMode: Text.WordWrap
        }
    }

    Text {
        Layout.fillWidth: true
        text: root.message
        font.pointSize: Theme.fontSize
        color: Theme.fgDefault
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: root.hint !== ""
        text: root.hint
        font.pointSize: Theme.fontSize
        color: Theme.fgMuted
        wrapMode: Text.WordWrap
    }

    QButton {
        id: toggle

        visible: root.detail !== ""
        variant: "flat"
        text: qsTr("Technical details")
        iconGlyph: root.expanded ? Icons.chevronDown : Icons.chevronRight
        onClicked: root.expanded = !root.expanded

        Accessible.expandable: true
        Accessible.expanded: root.expanded
    }

    TextEdit {
        id: detailText

        Layout.fillWidth: true
        visible: root.expanded && root.detail !== ""
        activeFocusOnTab: visible
        readOnly: true
        selectByMouse: true
        text: root.detail
        wrapMode: TextEdit.WrapAnywhere
        font.family: Theme.monoFontFamily
        font.pointSize: Theme.fontSize
        color: Theme.fgDefault
        selectionColor: Theme.selectionBg
        selectedTextColor: Theme.selectionFg

        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Technical details")
        Accessible.readOnly: true
        Accessible.focusable: true

        FocusRing {
            shown: detailText.activeFocus
        }
    }

    QButton {
        visible: detailText.visible
        text: qsTr("Copy details")
        onClicked: {
            detailText.selectAll();
            detailText.copy();
            detailText.deselect();
        }
    }
}
