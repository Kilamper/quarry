pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQml
import Quarry.Controls

// The application frame: toolbar, sidebar | (editor / results), status bar. Every action is a
// Command in CommandRegistry; the shortcuts, the menu and the palette all read that registry.
Item {
    id: shell

    // Regions in F6 order. Hidden ones are skipped.
    readonly property list<Item> regions: [sidebarRegion, editorRegion, resultsRegion, statusRegion]
    readonly property list<Command> ownCommands: [cmdPalette, cmdSettings, cmdNewConnection, cmdOpenSqlite, cmdEditConnection, cmdDuplicateConnection, cmdDeleteConnection, cmdDisconnect, cmdSidebar, cmdResults, cmdResetLayout, cmdZoomIn, cmdZoomOut, cmdZoomReset, cmdFocusSidebar, cmdFocusEditor, cmdFocusResults, cmdNextRegion, cmdPreviousRegion]
    // Overlays are created on first use (and preloaded when idle) to keep them off the startup
    // path; each is null until then.
    readonly property Item overlayItem: T.Overlay.overlay
    readonly property var paletteDialog: paletteLoader.item
    readonly property var settingsDialog: settingsLoader.item
    readonly property var connectionDialog: connectionLoader.item
    readonly property var deleteDialog: deleteLoader.item
    readonly property var filePicker: pickerLoader.item
    readonly property alias connections: connectionsCtl
    readonly property alias sidebar: sidebar
    readonly property var appMenu: menuLoader.item
    // Where the connections live; empty = the application's own. Tests use a temporary one.
    property string databasePath: ""
    property string providerDirectory: ""
    property bool sidebarVisible: shell.flag("sidebarVisible", true)
    property bool resultsVisible: shell.flag("resultsVisible", true)
    readonly property string selectedSummary: sidebar.hasCurrent ? qsTr("Selected connection: %1").arg(sidebar.currentName) + (sidebar.currentProduction ? ", " + qsTr("production") : "") : ""
    readonly property int defaultSidebarWidth: Theme.controlHeight * 10
    readonly property int defaultResultsHeight: Theme.controlHeight * 8

    // QSettings (INI) gives booleans back as strings.
    function flag(key, fallback) {
        const v = LayoutStore.value(key, fallback);
        return v === true || v === "true";
    }

    function showOverlay(loader) {
        loader.active = true;
        loader.item.open();
    }

    function openConnectionDialog(id, copied) {
        connectionLoader.active = true;
        connectionLoader.item.openFor(id, copied === true);
    }

    function askDelete(id) {
        const row = connectionsCtl.model.indexOf(id);
        if (row < 0)
            return;
        const c = connectionsCtl.model.get(row);
        deleteLoader.active = true;
        deleteLoader.item.openFor(id, c.name, c.production);
    }

    function regionOf(item) {
        for (let p = item; p; p = p.parent) {
            const i = regions.indexOf(p);
            if (i >= 0)
                return i;
        }
        return -1;
    }

    function focusRegion(index) {
        if (index === 0)
            sidebarVisible = true;
        else if (index === 2)
            resultsVisible = true;
        regions[index].focusDefault();
    }

    function cycleRegion(step) {
        const n = regions.length;
        const current = regionOf(Window.activeFocusItem);
        const from = current >= 0 ? current : (step > 0 ? -1 : 0);
        for (let k = 1; k <= n; ++k) {
            const j = ((from + step * k) % n + n) % n;
            if (regions[j].visible) {
                focusRegion(j);
                return;
            }
        }
    }

    // A pane that is about to disappear must not take the focus with it.
    function leaveRegion(index) {
        if (regionOf(Window.activeFocusItem) === index)
            focusRegion(1);
    }

    function toggleSidebar() {
        if (sidebarVisible)
            leaveRegion(0);
        sidebarVisible = !sidebarVisible;
    }

    function toggleResults() {
        if (resultsVisible)
            leaveRegion(2);
        resultsVisible = !resultsVisible;
    }

    function resetLayout() {
        saveLayout.stop();
        for (const key of ["sidebarVisible", "resultsVisible", "sidebarWidth", "resultsHeight"])
            LayoutStore.remove(key);
        sidebarVisible = true;
        resultsVisible = true;
        sidebarRegion.T.SplitView.preferredWidth = defaultSidebarWidth;
        resultsRegion.T.SplitView.preferredHeight = defaultResultsHeight;
    }

    onSidebarVisibleChanged: LayoutStore.setValue("sidebarVisible", sidebarVisible)
    onResultsVisibleChanged: LayoutStore.setValue("resultsVisible", resultsVisible)

    Component.onCompleted: {
        for (const c of ownCommands)
            CommandRegistry.registerCommand(c);
    }
    Component.onDestruction: {
        for (const c of ownCommands)
            CommandRegistry.unregisterCommand(c.commandId);
    }

    // Waits for the layout to settle after a resize, then stores the sizes of the visible panes.
    Timer {
        id: saveLayout

        interval: 300
        onTriggered: {
            if (sidebarRegion.visible)
                LayoutStore.setValue("sidebarWidth", Math.round(sidebarRegion.width));
            if (resultsRegion.visible)
                LayoutStore.setValue("resultsHeight", Math.round(resultsRegion.height));
        }
    }

    // ---- connections -------------------------------------------------------------------------

    ConnectionController {
        id: connectionsCtl

        databasePath: shell.databasePath
        providerDirectory: shell.providerDirectory

        onConnectionSaved: id => sidebar.selectId(id)
        // The copy has no password (it is never copied): when it needs one, the edit dialog opens
        // and says so.
        onConnectionDuplicated: id => {
            sidebar.selectId(id);
            if (connectionsCtl.model.get(connectionsCtl.model.indexOf(id)).passwordMode === 1)
                shell.openConnectionDialog(id, true);
        }
        onConnectionRemoved: Qt.callLater(() => {
            if (shell.regionOf(shell.Window.activeFocusItem) < 0)
                shell.focusRegion(0);
        })
    }

    // The provider scan and the store thread start after the first frame, never before it.
    Connections {
        target: shell.Window.window
        enabled: !connectionsCtl.started

        function onFrameSwapped() {
            connectionsCtl.start();
        }
    }

    // ---- commands ----------------------------------------------------------------------------

    Command {
        id: cmdPalette

        commandId: "palette.open"
        category: qsTr("General")
        title: qsTr("Command palette")
        shortcut: "Ctrl+K"
        alternateShortcut: "Ctrl+Shift+P"
        trackRecent: false // opening the palette must not push itself to the top of it
        onTriggered: shell.showOverlay(paletteLoader)
    }

    Command {
        id: cmdSettings

        commandId: "settings.open"
        category: qsTr("General")
        title: qsTr("Settings")
        shortcut: "Ctrl+,"
        onTriggered: shell.showOverlay(settingsLoader)
    }

    Command {
        id: cmdNewConnection

        commandId: "connection.new"
        category: qsTr("Connection")
        title: qsTr("New connection")
        shortcut: "Ctrl+Shift+N"
        onTriggered: shell.openConnectionDialog("", false)
    }

    Command {
        id: cmdOpenSqlite

        commandId: "connection.openSqlite"
        category: qsTr("Connection")
        title: qsTr("Open SQLite file")
        shortcut: "Ctrl+O"
        onTriggered: shell.showOverlay(pickerLoader)
    }

    Command {
        id: cmdEditConnection

        commandId: "connection.edit"
        category: qsTr("Connection")
        title: qsTr("Edit connection")
        enabled: sidebar.hasCurrent
        onTriggered: shell.openConnectionDialog(sidebar.currentId, false)
    }

    Command {
        id: cmdDuplicateConnection

        commandId: "connection.duplicate"
        category: qsTr("Connection")
        title: qsTr("Duplicate connection")
        enabled: sidebar.hasCurrent
        onTriggered: connectionsCtl.duplicate(sidebar.currentId)
    }

    Command {
        id: cmdDeleteConnection

        commandId: "connection.delete"
        category: qsTr("Connection")
        title: qsTr("Delete connection")
        enabled: sidebar.hasCurrent
        onTriggered: shell.askDelete(sidebar.currentId)
    }

    Command {
        id: cmdDisconnect

        commandId: "connection.disconnect"
        category: qsTr("Connection")
        title: qsTr("Disconnect")
        enabled: sidebar.currentConnected
        onTriggered: connectionsCtl.tree.disconnectConnection(sidebar.currentId)
    }

    Command {
        id: cmdSidebar

        commandId: "view.toggleSidebar"
        category: qsTr("View")
        title: qsTr("Toggle sidebar")
        shortcut: "Ctrl+B"
        checkable: true
        checked: shell.sidebarVisible
        onTriggered: shell.toggleSidebar()
    }

    Command {
        id: cmdResults

        commandId: "view.toggleResults"
        category: qsTr("View")
        title: qsTr("Toggle results panel")
        shortcut: "Ctrl+J"
        checkable: true
        checked: shell.resultsVisible
        onTriggered: shell.toggleResults()
    }

    Command {
        id: cmdResetLayout

        commandId: "view.resetLayout"
        category: qsTr("View")
        title: qsTr("Reset layout")
        onTriggered: shell.resetLayout()
    }

    Command {
        id: cmdZoomIn

        commandId: "view.zoomIn"
        category: qsTr("View")
        title: qsTr("Zoom in")
        shortcut: "Ctrl+="
        alternateShortcut: "Ctrl++"
        onTriggered: Theme.zoomIn()
    }

    Command {
        id: cmdZoomOut

        commandId: "view.zoomOut"
        category: qsTr("View")
        title: qsTr("Zoom out")
        shortcut: "Ctrl+-"
        onTriggered: Theme.zoomOut()
    }

    Command {
        id: cmdZoomReset

        commandId: "view.zoomReset"
        category: qsTr("View")
        title: qsTr("Reset zoom")
        shortcut: "Ctrl+0"
        onTriggered: Theme.resetZoom()
    }

    Command {
        id: cmdFocusSidebar

        commandId: "focus.sidebar"
        category: qsTr("Navigate")
        title: qsTr("Go to sidebar")
        shortcut: "Ctrl+1"
        onTriggered: shell.focusRegion(0)
    }

    Command {
        id: cmdFocusEditor

        commandId: "focus.editor"
        category: qsTr("Navigate")
        title: qsTr("Go to editor")
        shortcut: "Ctrl+2"
        onTriggered: shell.focusRegion(1)
    }

    Command {
        id: cmdFocusResults

        commandId: "focus.results"
        category: qsTr("Navigate")
        title: qsTr("Go to results")
        shortcut: "Ctrl+3"
        onTriggered: shell.focusRegion(2)
    }

    Command {
        id: cmdNextRegion

        commandId: "focus.nextRegion"
        category: qsTr("Navigate")
        title: qsTr("Next region")
        shortcut: "F6"
        onTriggered: shell.cycleRegion(1)
    }

    Command {
        id: cmdPreviousRegion

        commandId: "focus.previousRegion"
        category: qsTr("Navigate")
        title: qsTr("Previous region")
        shortcut: "Shift+F6"
        onTriggered: shell.cycleRegion(-1)
    }

    Instantiator {
        model: CommandRegistry.commands

        delegate: Shortcut {
            required property var modelData

            sequences: modelData.sequences
            enabled: modelData.enabled
            onActivated: CommandRegistry.trigger(modelData.commandId)
        }
    }

    // ---- layout ------------------------------------------------------------------------------

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: toolbar.implicitHeight + Theme.spacing * 2
            color: Theme.bgSurface

            Accessible.role: Accessible.ToolBar
            Accessible.name: qsTr("Toolbar")

            // Bottom edge, so the toolbar is delimited without relying on the fill color.
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderDefault
            }

            RowLayout {
                id: toolbar

                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacing

                IconButton {
                    id: menuButton

                    accessibleName: qsTr("Menu")
                    iconGlyph: Icons.menu
                    onClicked: {
                        menuLoader.active = true;
                        (menuLoader.item as QMenu).popup(menuButton, 0, menuButton.height);
                    }
                }

                IconButton {
                    accessibleName: cmdSidebar.title
                    shortcutText: cmdSidebar.shortcutText
                    iconGlyph: Icons.panelLeft
                    onClicked: CommandRegistry.trigger(cmdSidebar.commandId)
                }

                IconButton {
                    accessibleName: cmdResults.title
                    shortcutText: cmdResults.shortcutText
                    iconGlyph: Icons.panelBottom
                    onClicked: CommandRegistry.trigger(cmdResults.commandId)
                }

                IconButton {
                    accessibleName: cmdResetLayout.title
                    iconGlyph: Icons.rotateCcw
                    onClicked: CommandRegistry.trigger(cmdResetLayout.commandId)
                }

                Item {
                    Layout.fillWidth: true
                }

                IconButton {
                    accessibleName: cmdPalette.title
                    shortcutText: cmdPalette.shortcutText
                    iconGlyph: Icons.command
                    onClicked: CommandRegistry.trigger(cmdPalette.commandId)
                }
            }
        }

        QSplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            accessibleName: qsTr("Resize sidebar")

            onResized: saveLayout.restart()

            Region {
                id: sidebarRegion

                regionName: qsTr("Sidebar")
                defaultFocus: sidebar.defaultFocus
                visible: shell.sidebarVisible
                T.SplitView.preferredWidth: Number(LayoutStore.value("sidebarWidth", shell.defaultSidebarWidth))
                T.SplitView.minimumWidth: Theme.controlHeight * 6
                T.SplitView.maximumWidth: Math.max(T.SplitView.minimumWidth, shell.width * 0.6)

                ConnectionList {
                    id: sidebar

                    anchors.fill: parent
                    connections: connectionsCtl
                    newShortcut: cmdNewConnection.shortcutText
                    onNewRequested: CommandRegistry.trigger(cmdNewConnection.commandId)
                    onOpenSqliteRequested: CommandRegistry.trigger(cmdOpenSqlite.commandId)
                    onEditRequested: id => shell.openConnectionDialog(id, false)
                    onDuplicateRequested: id => connectionsCtl.duplicate(id)
                    onDeleteRequested: id => shell.askDelete(id)
                }
            }

            QSplitView {
                T.SplitView.fillWidth: true
                T.SplitView.minimumWidth: Theme.controlHeight * 8
                orientation: Qt.Vertical
                accessibleName: qsTr("Resize results panel")

                onResized: saveLayout.restart()

                Region {
                    id: editorRegion

                    regionName: qsTr("Editor")
                    color: Theme.bgCanvas
                    defaultFocus: editorEmpty
                    T.SplitView.fillHeight: true
                    T.SplitView.minimumHeight: Theme.controlHeight * 4

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        QTabBar {
                            Layout.fillWidth: true
                            accessibleName: qsTr("Editor tabs")
                        }

                        EmptyState {
                            id: editorEmpty

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            focusable: true
                            iconGlyph: Icons.fileCode
                            title: qsTr("No query open")
                            message: qsTr("Write SQL here. %1 runs the current statement.").arg(CommandRegistry.displayShortcut("Ctrl+Enter"))
                        }
                    }
                }

                Region {
                    id: resultsRegion

                    regionName: qsTr("Results panel")
                    color: Theme.bgCanvas
                    defaultFocus: resultsTabs.currentItem
                    visible: shell.resultsVisible
                    T.SplitView.preferredHeight: Number(LayoutStore.value("resultsHeight", shell.defaultResultsHeight))
                    T.SplitView.minimumHeight: Theme.controlHeight * 4

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        QTabBar {
                            id: resultsTabs

                            Layout.fillWidth: true
                            accessibleName: qsTr("Results panel tabs")

                            QTabButton {
                                text: qsTr("Results")
                            }

                            QTabButton {
                                text: qsTr("Messages")
                            }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: resultsTabs.currentIndex

                            EmptyState {
                                iconGlyph: Icons.table
                                title: qsTr("No results yet")
                                message: qsTr("Run a query to see results here.")
                            }

                            EmptyState {
                                iconGlyph: Icons.info
                                title: qsTr("No messages")
                                message: qsTr("Errors, notices and affected row counts appear here.")
                            }
                        }
                    }
                }
            }
        }

        Region {
            id: statusRegion

            Layout.fillWidth: true
            implicitHeight: Math.max(Theme.controlHeight, statusText.implicitHeight + Theme.spacing * 2)
            regionName: qsTr("Status bar")
            defaultFocus: statusItem

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.borderDefault
            }

            // Focusable so keyboard and screen-reader users can read the status; later milestones
            // add the running query, row count and so on beside it.
            Item {
                id: statusItem

                anchors.fill: parent
                activeFocusOnTab: true

                Accessible.role: Accessible.StaticText
                Accessible.name: statusText.text + (sidebar.hasCurrent ? ". " + shell.selectedSummary : "")
                Accessible.focusable: true

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing * 2
                    anchors.rightMargin: Theme.spacing * 2
                    spacing: Theme.spacing

                    Icon {
                        text: sidebar.currentConnected ? Icons.plug : Icons.unplug
                        color: Theme.fgMuted
                    }

                    Text {
                        id: statusText

                        Layout.fillWidth: true
                        text: sidebar.currentConnected ? qsTr("Connected") : qsTr("Not connected")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgDefault
                        wrapMode: Text.WordWrap
                    }

                    // The connection of the selected node.
                    Text {
                        visible: sidebar.hasCurrent
                        text: qsTr("Selected: %1").arg(sidebar.currentName)
                        font.pointSize: Theme.fontSize
                        color: Theme.fgMuted
                    }

                    ProductionBadge {
                        visible: sidebar.hasCurrent && sidebar.currentProduction
                    }
                }

                FocusRing {
                    shown: statusItem.activeFocus
                    inset: true
                }
            }
        }
    }

    // ---- overlays ----------------------------------------------------------------------------

    Loader {
        id: paletteLoader

        active: false
        sourceComponent: CommandPalette {}
    }

    Loader {
        id: settingsLoader

        active: false
        sourceComponent: SettingsDialog {}
    }

    Loader {
        id: connectionLoader

        active: false
        sourceComponent: ConnectionDialog {
            connections: connectionsCtl
        }
    }

    Loader {
        id: deleteLoader

        active: false
        sourceComponent: DeleteConnectionDialog {
            onDeleteConfirmed: id => connectionsCtl.remove(id)
        }
    }

    // "Open SQLite file": the platform file dialog, created on first use.
    Loader {
        id: pickerLoader

        // By URL, not as a type: naming FilePicker here would load QtQuick.Dialogs at startup.
        active: false
        source: "FilePicker.qml"
    }

    Connections {
        target: pickerLoader.item

        function onAccepted() {
            // qmllint disable missing-property
            connectionsCtl.openSqliteFile(pickerLoader.item["selectedFile"]);
            // qmllint enable missing-property
        }
    }

    Loader {
        id: menuLoader

        active: false
        sourceComponent: QMenu {
            id: appMenu

            // The chosen command runs once the menu is closed, so its focus change sticks.
            property string pendingId: ""

            title: qsTr("Menu")
            onClosed: {
                if (pendingId !== "") {
                    const id = pendingId;
                    pendingId = "";
                    Qt.callLater(() => CommandRegistry.trigger(id));
                }
            }

            Instantiator {
                model: CommandRegistry.commands

                delegate: QMenuItem {
                    required property var modelData

                    text: modelData.title
                    shortcutText: modelData.shortcutText
                    enabled: modelData.enabled
                    checkable: modelData.checkable
                    checked: modelData.checked
                    onTriggered: appMenu.pendingId = modelData.commandId
                }

                onObjectAdded: (index, object) => appMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => appMenu.removeItem(object)
            }
        }
    }

    // Idle preload, so the first Ctrl+K or Ctrl+, opens within a frame.
    Timer {
        interval: 1500
        running: true
        onTriggered: {
            paletteLoader.active = true;
            settingsLoader.active = true;
        }
    }
}
