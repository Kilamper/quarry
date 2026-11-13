pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Quarry.Controls

// Palette over CommandRegistry (Mod+K, Mod+Shift+P). Focus stays in the search field; Up/Down
// move the current row, Enter runs it, Esc closes (QDialog returns focus to the opener).
QDialog {
    id: root

    // The command runs after the palette has closed, so it can move focus without being undone.
    property string pendingId: ""

    function runCurrent() {
        const row = list.currentItem as PaletteRow;
        if (!row || !row.commandEnabled)
            return;
        pendingId = row.commandId;
        close();
    }

    component PaletteRow: Item {
        id: row

        required property int index
        required property string commandId
        required property string title
        required property string category
        required property string shortcutText
        required property bool commandEnabled

        width: ListView.view.width
        height: Math.max(Theme.controlHeight, line.implicitHeight + Theme.spacing * 2)
        opacity: commandEnabled ? 1 : 0.6

        Accessible.role: Accessible.ListItem
        Accessible.name: title + ", " + category + (shortcutText !== "" ? ", " + shortcutText : "")
        Accessible.selected: ListView.isCurrentItem

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: row.ListView.isCurrentItem || hover.hovered ? Theme.bgHover : Qt.alpha(Theme.bgRaised, 0)

            // The current row is marked by a bar, not only by its fill.
            Rectangle {
                visible: row.ListView.isCurrentItem
                width: Theme.focusRingWidth
                height: parent.height
                color: Theme.accent
            }
        }

        RowLayout {
            id: line

            anchors.fill: parent
            anchors.leftMargin: Theme.spacing * 2
            anchors.rightMargin: Theme.spacing * 2
            spacing: Theme.spacing * 2

            Text {
                Layout.fillWidth: true
                text: row.title
                font.pointSize: Theme.fontSize
                color: row.commandEnabled ? Theme.fgDefault : Theme.fgDisabled
                wrapMode: Text.WordWrap
            }

            Text {
                text: row.category
                font.pointSize: Theme.fontSize
                color: Theme.fgMuted
            }

            Text {
                visible: row.shortcutText !== ""
                text: row.shortcutText
                font.pointSize: Theme.fontSize
                color: Theme.fgMuted
            }
        }

        HoverHandler {
            id: hover
        }

        TapHandler {
            onTapped: {
                list.currentIndex = row.index;
                root.runCurrent();
            }
        }
    }

    anchors.centerIn: undefined
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height / 10)
    width: Math.min(parent.width - Theme.controlHeight, Theme.controlHeight * 20)
    padding: Theme.spacing * 2
    title: qsTr("Command palette")
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    onAboutToShow: {
        field.text = "";
        CommandRegistry.filter = "";
        list.currentIndex = 0;
        announce.stop();
    }
    onOpened: field.forceActiveFocus(Qt.TabFocusReason)
    onClosed: {
        if (pendingId !== "") {
            const id = pendingId;
            pendingId = "";
            Qt.callLater(() => CommandRegistry.trigger(id));
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing

        QTextField {
            id: field

            Layout.fillWidth: true
            accessibleName: qsTr("Search commands")
            placeholderText: qsTr("Type a command")

            onTextChanged: {
                CommandRegistry.filter = text;
                list.currentIndex = list.count > 0 ? 0 : -1;
                announce.restart();
            }
            onAccepted: root.runCurrent()
            Keys.onDownPressed: list.currentIndex = Math.min(list.currentIndex + 1, list.count - 1)
            Keys.onUpPressed: list.currentIndex = Math.max(list.currentIndex - 1, 0)
        }

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Theme.controlHeight * 10)
            visible: count > 0
            clip: true
            model: CommandRegistry
            currentIndex: 0
            keyNavigationEnabled: false

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Commands")

            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: PaletteRow {}
        }

        Text {
            Layout.fillWidth: true
            visible: list.count === 0
            text: qsTr("No commands match '%1'").arg(field.text)
            font.pointSize: Theme.fontSize
            color: Theme.fgMuted
            wrapMode: Text.WordWrap
        }

        Timer {
            id: announce

            interval: 250
            onTriggered: field.Accessible.announce(list.count === 0 ? qsTr("No commands match '%1'").arg(field.text) : list.count === 1 ? qsTr("1 command") : qsTr("%1 commands").arg(list.count))
        }
    }
}
