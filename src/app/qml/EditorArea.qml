pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// The content of the Editor region: the tab bar, the connection of the current tab, the editors
// (one per tab, created the first time the tab is shown and kept so each keeps its undo history
// and scroll position) and the empty state when no tab is open.
ColumnLayout {
    id: root

    required property var tabs // EditorTabsModel
    required property var connections // ConnectionController
    property string newShortcut: ""
    // The last thing sent to the screen reader (tests read it; it is not shown).
    property string lastAnnouncement: ""
    property string errorMessage: ""
    property string errorDetail: ""
    // Bumped when the connection list changes in a way `indexOf` cannot signal.
    property int revision: 0
    property int editorRevision: 0

    // The editor of the current tab, null when there is none (or it is not created yet).
    readonly property var currentEditor: {
        const holder = root.editorRevision >= 0 ? editors.itemAt(root.tabs.currentIndex) as Loader : null;
        return holder ? holder.item : null;
    }
    readonly property Item defaultFocus: currentEditor ? currentEditor.edit : emptyView
    readonly property alias tabBar: bar
    readonly property alias connectionCombo: combo
    readonly property alias emptyState: emptyView

    signal newRequested
    signal closeRequested(int row)

    function announce(text, assertive) {
        lastAnnouncement = text;
        root.Accessible.announce(text, assertive ? Accessible.Assertive : Accessible.Polite);
    }

    spacing: 0

    Connections {
        target: root.tabs

        function onFailed(message, detail) {
            root.errorMessage = message;
            root.errorDetail = detail;
            root.announce(message, true);
        }
    }

    Connections {
        target: root.connections.model

        function onDataChanged() {
            root.revision++;
        }

        function onRowsMoved() {
            root.revision++;
        }

        function onModelReset() {
            root.revision++;
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: false // its fill-height child would otherwise make the whole row grow
        spacing: 0

        QTabBar {
            id: bar

            Layout.fillWidth: true
            accessibleName: qsTr("Editor tabs")
            currentIndex: root.tabs.currentIndex
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex !== root.tabs.currentIndex)
                    root.tabs.currentIndex = currentIndex;
            }

            Repeater {
                model: root.tabs

                QTabButton {
                    id: tab

                    required property int index
                    required property string title
                    required property string connectionName
                    required property int colorIndex
                    required property bool production

                    text: title
                    closable: true
                    Accessible.name: title + (connectionName !== "" ? ", " + connectionName : "") + (production ? ", " + qsTr("production") : "")
                    onCloseRequested: root.closeRequested(index)

                    leading: Rectangle {
                        visible: tab.colorIndex >= 0
                        implicitWidth: Theme.spacing * 3
                        implicitHeight: implicitWidth
                        radius: width / 2
                        color: tab.colorIndex >= 0 ? Theme.connectionColor(tab.colorIndex) : Qt.alpha(Theme.bgSurface, 0)
                        border.width: 1
                        border.color: Theme.borderStrong
                    }

                    trailing: ProductionBadge {
                        visible: tab.production
                    }
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: newButton.implicitWidth + Theme.spacing
            color: Theme.bgSurface

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderDefault
            }

            IconButton {
                id: newButton

                anchors.centerIn: parent
                accessibleName: qsTr("New query")
                shortcutText: root.newShortcut
                iconGlyph: Icons.plus
                onClicked: root.newRequested()
            }
        }
    }

    // The connection of the current tab: its color and PRODUCTION are in the tab, the name and the
    // chooser here.
    // On one line when it fits; otherwise the PRODUCTION badge drops below instead of squeezing
    // the chooser (its name would be elided) or leaving the window at 200% zoom.
    GridLayout {
        id: connectionRow

        readonly property bool fits: width >= connectionLabel.implicitWidth + combo.implicitWidth + connectionBadge.implicitWidth + columnSpacing * 2

        Layout.fillWidth: true
        Layout.margins: Theme.spacing
        visible: root.tabs.count > 0
        columns: 3
        columnSpacing: Theme.spacing * 2
        rowSpacing: Theme.spacing

        Text {
            id: connectionLabel

            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Connection")
            font.pointSize: Theme.fontSize
            color: Theme.fgMuted
            Accessible.ignored: true // the combo carries the name
        }

        QComboBox {
            id: combo

            Layout.fillWidth: true
            Layout.maximumWidth: Theme.controlHeight * 12
            accessibleName: qsTr("Connection of %1").arg(root.currentEditor?.tabTitle ?? "")
            model: root.connections.model
            textRole: "name"
            displayText: currentIndex < 0 ? qsTr("Choose a connection") : currentText
            currentIndex: root.revision + root.connections.model.count >= 0 ? root.connections.model.indexOf(root.currentEditor?.connectionId ?? "") : -1
            onActivated: index => root.tabs.setConnection(root.tabs.currentIndex, root.connections.model.get(index).connectionId)
        }

        ProductionBadge {
            id: connectionBadge

            Layout.row: connectionRow.fits ? 0 : 1
            Layout.column: connectionRow.fits ? 2 : 1
            Layout.alignment: Qt.AlignVCenter
            visible: root.currentEditor?.production ?? false
        }
    }

    // A tab could not be stored: said in plain language, with the driver text below.
    Loader {
        Layout.fillWidth: true
        Layout.margins: Theme.spacing * 2
        active: root.errorMessage !== ""
        visible: active
        sourceComponent: ErrorPanel {
            title: qsTr("Your queries could not be saved")
            message: root.errorMessage
            detail: root.errorDetail
        }
    }

    StackLayout {
        id: stack

        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: root.tabs.count > 0
        currentIndex: root.tabs.currentIndex

        Repeater {
            id: editors

            model: root.tabs

            onItemAdded: root.editorRevision++
            onItemRemoved: root.editorRevision++

            Loader {
                id: holder

                required property int index
                required property string title
                required property string connectionId
                required property string connectionName
                required property string definition
                required property bool production
                property bool shown: false

                // Sized from the stack itself: a Loader that turns active after the stack's last
                // layout pass would otherwise stay 0x0 until the next one (seen on Linux CI).
                width: stack.width
                height: stack.height
                active: shown || StackLayout.isCurrentItem
                onItemChanged: root.editorRevision++
                StackLayout.onIsCurrentItemChanged: {
                    if (StackLayout.isCurrentItem)
                        shown = true;
                }

                sourceComponent: SqlEditor {
                    anchors.fill: parent
                    tabs: root.tabs
                    row: holder.index
                    tabTitle: holder.title
                    connectionId: holder.connectionId
                    connectionName: holder.connectionName
                    definition: holder.definition
                    production: holder.production
                    placeholder: qsTr("Write SQL here. %1 runs the current statement.").arg(CommandRegistry.displayShortcut("Ctrl+Enter"))
                }
            }
        }
    }

    EmptyState {
        id: emptyView

        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: root.tabs.count === 0
        focusable: true
        iconGlyph: Icons.fileCode
        title: qsTr("No query open")
        message: qsTr("Write SQL here. %1 runs the current statement.").arg(CommandRegistry.displayShortcut("Ctrl+Enter"))

        QButton {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("New query")
            iconGlyph: Icons.plus
            variant: "primary"
            onClicked: root.newRequested()
        }
    }
}
