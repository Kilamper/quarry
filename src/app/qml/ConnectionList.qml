pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// The sidebar content: one tree. The saved connections are its roots; expanding one connects and
// lists its schema lazily (database, folders, tables, columns...). Keyboard: Up/Down move, Right
// expands or goes to the first child, Left collapses or goes to the parent, Home/End, Enter
// toggles, letters jump to the next node starting with them, Delete deletes a connection after
// confirming, Shift+F10 / Menu / right click open the context menu. Collapsing does not
// disconnect; "Disconnect" does. There is one Tab stop for the whole tree (roving selection).
Item {
    id: root

    required property var connections // ConnectionController
    property string newShortcut: ""
    // The last thing sent to the screen reader (tests read it; it is not shown).
    property string lastAnnouncement: ""
    // Bumped when the data of a node changes, so the "current" properties below re-read it.
    property int revision: 0

    readonly property alias tree: treeLoader.item
    readonly property alias newButton: newBtn
    readonly property alias menu: actionMenu
    readonly property var passwordDialog: passwordLoader.item
    // Always read from the selection model: it holds a persistent index, so it never goes stale
    // when nodes are replaced (a copy kept in a property would).
    readonly property bool hasCurrent: tree !== null && tree.selectionModel.currentIndex.valid
    // The connection the current node belongs to.
    readonly property string currentId: {
        root.revision;
        return tree ? connections.tree.connectionIdOf(tree.selectionModel.currentIndex) : "";
    }
    readonly property string currentName: rootRole(SchemaTreeModel.NameRole, "")
    readonly property bool currentProduction: rootRole(SchemaTreeModel.ProductionRole, false)
    readonly property bool currentConnected: hasCurrent && connections.tree.connectedIds.includes(currentId)
    // First thing F6 / Mod+1 should land on.
    readonly property Item defaultFocus: tree ?? (newBtn.visible ? newBtn : headerNew)

    signal newRequested
    signal openSqliteRequested
    signal editRequested(string id)
    signal duplicateRequested(string id)
    signal deleteRequested(string id)

    function rootRole(role, fallback) {
        root.revision;
        const idx = tree ? tree.selectionModel.currentIndex : null;
        return idx?.valid ? connections.tree.data(connections.tree.connectionOf(idx), role) : fallback;
    }

    function selectId(id) {
        const idx = connections.tree.indexOfConnection(id);
        if (!idx.valid || !tree)
            return;
        tree.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect);
        const row = tree.rowAtIndex(idx);
        if (row >= 0)
            tree.positionViewAtRow(row, TableView.Contain);
    }

    function popupMenu(item, index) {
        actionMenu.targetId = connections.tree.connectionIdOf(index);
        actionMenu.popup(item, Theme.spacing * 2, item === root.tree ? 0 : item.height);
    }

    function announce(text, assertive) {
        lastAnnouncement = text;
        (tree ?? root).Accessible.announce(text, assertive ? Accessible.Assertive : Accessible.Polite);
    }

    Connections {
        target: root.connections.tree

        function onAnnouncement(text, assertive) {
            root.announce(text, assertive);
        }

        // Disconnected, or the password prompt was dismissed: show the branch collapsed.
        function onCollapseRequested(index) {
            const row = root.tree ? root.tree.rowAtIndex(index) : -1;
            if (row >= 0 && root.tree.isExpanded(row))
                root.tree.collapseRow(row);
        }

        function onPasswordRequested(id, name, retry) {
            passwordLoader.active = true;
            passwordLoader.item.openFor(id, name, retry);
        }

        function onDataChanged() {
            root.revision++;
        }

        function onModelReset() {
            root.revision++;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 2
        spacing: Theme.spacing * 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Heading {
                Layout.fillWidth: true
                level: 2
                text: qsTr("Connections")
            }

            IconButton {
                id: headerNew

                accessibleName: qsTr("New connection")
                shortcutText: root.newShortcut
                iconGlyph: Icons.plus
                onClicked: root.newRequested()
            }
        }

        // Load failure, or a failed delete / duplicate / open.
        ColumnLayout {
            id: banner

            readonly property string message: root.connections.errorMessage !== "" ? root.connections.errorMessage : root.connections.model.lastError
            readonly property string detail: root.connections.errorMessage !== "" ? root.connections.errorDetail : root.connections.model.lastErrorDetail

            Layout.fillWidth: true
            visible: message !== ""
            spacing: Theme.spacing

            onMessageChanged: {
                if (message !== "")
                    banner.Accessible.announce(message, Accessible.Assertive);
            }

            // Created when there is something to say: it keeps its text engine off the startup path.
            Loader {
                Layout.fillWidth: true
                active: banner.visible
                sourceComponent: ErrorPanel {
                    title: root.connections.errorMessage !== "" ? qsTr("Something went wrong") : qsTr("Could not load the connections")
                    message: banner.message
                    detail: banner.detail
                }
            }

            RowLayout {
                spacing: Theme.spacing

                QButton {
                    visible: root.connections.errorMessage === ""
                    text: qsTr("Try again")
                    onClicked: root.connections.model.start()
                }

                QButton {
                    visible: root.connections.errorMessage !== ""
                    text: qsTr("Dismiss")
                    onClicked: root.connections.clearError()
                }
            }
        }

        // Loading: shown instead of the empty state so the first frame does not claim "nothing".
        RowLayout {
            Layout.fillWidth: true
            visible: root.connections.model.loading
            spacing: Theme.spacing

            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Loading connections…")

            Icon {
                id: spinner

                text: Icons.loaderCircle
                color: Theme.fgMuted

                RotationAnimator on rotation {
                    running: spinner.visible && Theme.animationDuration > 0
                    from: 0
                    to: 360
                    duration: 1000
                    loops: Animation.Infinite
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Loading connections…")
                font.pointSize: Theme.fontSize
                color: Theme.fgMuted
                wrapMode: Text.WordWrap
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.connections.model.loading && root.connections.model.count === 0 && root.connections.model.lastError === ""
            iconGlyph: Icons.database
            title: qsTr("No connections yet")
            message: qsTr("Add a connection to browse its tables and run queries.")

            QButton {
                id: newBtn

                Layout.alignment: Qt.AlignHCenter
                text: qsTr("New connection")
                variant: "primary"
                iconGlyph: Icons.plus
                onClicked: root.newRequested()
            }

            QButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Open SQLite file")
                iconGlyph: Icons.folderOpen
                onClicked: root.openSqliteRequested()
            }
        }

        // Created when the first connection exists: nothing of the tree (TreeView, delegates)
        // is loaded on the startup path, which only ever shows the empty state.
        Loader {
            id: treeLoader

            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.connections.model.count > 0

            sourceComponent: TreeView {
                id: treeView

                // Type-ahead state: what has been typed in the last second.
                property string typed: ""

                anchors.fill: parent
                clip: true
                model: root.connections.tree
                selectionModel: ItemSelectionModel {
                    model: treeView.model
                }
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                // Everything below is done by hand, so the keys do exactly what UX.md says.
                keyNavigationEnabled: false
                pointerNavigationEnabled: false
                editTriggers: TableView.NoEditTriggers
                columnWidthProvider: () => treeView.width
                onWidthChanged: forceLayout()

                Accessible.role: Accessible.Tree
                Accessible.name: qsTr("Connections")
                Accessible.focusable: true

                function cursorRow() {
                    return selectionModel.currentIndex.valid ? rowAtIndex(selectionModel.currentIndex) : -1;
                }

                function goTo(row) {
                    if (row < 0 || row >= rows)
                        return;
                    selectionModel.setCurrentIndex(index(row, 0), ItemSelectionModel.ClearAndSelect);
                    positionViewAtRow(row, TableView.Contain);
                }

                function roleAt(row, role) {
                    return root.connections.tree.data(index(row, 0), role);
                }

                function parentRow(row) {
                    const d = depth(row);
                    for (let r = row - 1; r >= 0; --r)
                        if (depth(r) < d)
                            return r;
                    return -1;
                }

                function hasChildNodes(row) {
                    return root.connections.tree.hasChildren(index(row, 0));
                }

                // Expands and, for a node not loaded yet, asks the model for its children.
                function expandRow(row) {
                    if (isExpanded(row))
                        return;
                    expand(row);
                    const i = index(row, 0);
                    if (root.connections.tree.canFetchMore(i))
                        root.connections.tree.fetchMore(i);
                }

                // Keeps the current node visible: if it was inside the branch, the branch takes it.
                function collapseRow(row) {
                    collapse(row);
                    if (cursorRow() < 0)
                        goTo(row);
                }

                function toggleRow(row) {
                    if (isExpanded(row))
                        collapseRow(row);
                    else
                        expandRow(row);
                }

                function activateRow(row) {
                    if (roleAt(row, SchemaTreeModel.KindRole) === "error")
                        root.connections.tree.retry(index(row, 0));
                    else if (hasChildNodes(row))
                        toggleRow(row);
                }

                function typeAhead(ch) {
                    typed += ch;
                    typeTimer.restart();
                    const prefix = typed.toLowerCase();
                    const cur = cursorRow();
                    // Typing on keeps the current node while it still matches.
                    const first = typed.length > 1 ? Math.max(cur, 0) : cur + 1;
                    for (let k = 0; k < rows; ++k) {
                        const r = (first + k) % rows;
                        if (String(roleAt(r, SchemaTreeModel.NameRole)).toLowerCase().startsWith(prefix)) {
                            const kind = roleAt(r, SchemaTreeModel.KindRole);
                            if (kind !== "loading" && kind !== "error" && kind !== "empty") {
                                goTo(r);
                                return;
                            }
                        }
                    }
                }

                function popupAtCurrent() {
                    const idx = selectionModel.currentIndex;
                    if (!idx.valid)
                        return;
                    root.popupMenu(itemAtIndex(idx) ?? treeView, idx);
                }

                onActiveFocusChanged: {
                    if (activeFocus && !selectionModel.currentIndex.valid && rows > 0)
                        goTo(0);
                }

                Keys.onReturnPressed: if (cursorRow() >= 0) activateRow(cursorRow())
                Keys.onEnterPressed: if (cursorRow() >= 0) activateRow(cursorRow())
                Keys.onMenuPressed: popupAtCurrent()
                Keys.onDeletePressed: {
                    const row = cursorRow();
                    if (row >= 0 && roleAt(row, SchemaTreeModel.KindRole) === "connection")
                        root.deleteRequested(roleAt(row, SchemaTreeModel.ConnectionIdRole));
                }
                Keys.onPressed: event => {
                    const row = cursorRow();
                    const page = Math.max(1, Math.floor(height / Theme.controlHeight) - 1);
                    event.accepted = true;
                    switch (event.key) {
                    case Qt.Key_Down:
                        goTo(Math.min(row + 1, rows - 1));
                        return;
                    case Qt.Key_Up:
                        goTo(Math.max(row - 1, 0));
                        return;
                    case Qt.Key_PageDown:
                        goTo(Math.min(Math.max(row, 0) + page, rows - 1));
                        return;
                    case Qt.Key_PageUp:
                        goTo(Math.max(row - page, 0));
                        return;
                    case Qt.Key_Home:
                        goTo(0);
                        return;
                    case Qt.Key_End:
                        goTo(rows - 1);
                        return;
                    case Qt.Key_Right:
                        if (row < 0)
                            goTo(0);
                        else if (hasChildNodes(row) && !isExpanded(row))
                            expandRow(row);
                        else if (isExpanded(row) && row + 1 < rows && depth(row + 1) > depth(row))
                            goTo(row + 1);
                        return;
                    case Qt.Key_Left:
                        if (row >= 0 && isExpanded(row))
                            collapseRow(row);
                        else if (row >= 0)
                            goTo(parentRow(row));
                        return;
                    case Qt.Key_F10:
                        if (event.modifiers & Qt.ShiftModifier) {
                            popupAtCurrent();
                            return;
                        }
                        break;
                    case Qt.Key_C:
                        // Copies the technical details of an error row.
                        if ((event.modifiers & Qt.ControlModifier) && row >= 0 && roleAt(row, SchemaTreeModel.KindRole) === "error") {
                            // qmllint disable missing-property
                            itemAtIndex(selectionModel.currentIndex)?.copyDetails();
                            // qmllint enable missing-property
                            return;
                        }
                        break;
                    case Qt.Key_Space:
                        if (typed === "" && row >= 0 && roleAt(row, SchemaTreeModel.KindRole) === "error") {
                            // qmllint disable missing-property
                            itemAtIndex(selectionModel.currentIndex)?.toggleDetails();
                            // qmllint enable missing-property
                            return;
                        }
                        break;
                    }
                    // Printable character without Ctrl/Alt/Meta: type-ahead.
                    if (!(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) && event.text.length === 1 && event.text >= " ") {
                        typeAhead(event.text);
                        return;
                    }
                    event.accepted = false;
                }

                Timer {
                    id: typeTimer

                    interval: 1000
                    onTriggered: treeView.typed = ""
                }

                delegate: Item {
                    id: node

                    required property TreeView treeView
                    required property bool isTreeNode
                    required property bool expanded
                    required property bool hasChildren
                    required property int depth
                    required property int row
                    required property int column
                    required property bool current
                    required property string name
                    required property string kind
                    required property string detail
                    required property string connectionId
                    required property string providerName
                    required property string summary
                    required property var colorIndex
                    required property string colorName
                    required property var production
                    required property bool connected
                    required property int position
                    required property int setSize

                    readonly property bool isConnection: kind === "connection"
                    readonly property bool isError: kind === "error"
                    readonly property bool isLoading: kind === "loading"
                    readonly property bool expandable: isTreeNode && hasChildren
                    readonly property bool hasColor: colorIndex !== undefined && colorIndex >= 0
                    property bool detailsOpen: false
                    readonly property color textColor: current ? Theme.selectionFg : Theme.fgDefault
                    readonly property color mutedColor: current ? Theme.selectionFg : Theme.fgMuted

                    function toggleDetails() {
                        detailsOpen = !detailsOpen;
                    }

                    function copyDetails() {
                        clipboard.selectAll();
                        clipboard.copy();
                        clipboard.deselect();
                    }

                    function iconFor(k) {
                        switch (k) {
                        case "connection":
                        case "database":
                            return Icons.database;
                        case "schema":
                            return Icons.layers;
                        case "folder":
                            return expanded ? Icons.folderOpen : Icons.folder;
                        case "table":
                            return Icons.table;
                        case "view":
                        case "materializedView":
                            return Icons.eye;
                        case "column":
                            return Icons.columns3;
                        case "index":
                            return Icons.hash;
                        case "foreignKey":
                            return Icons.link;
                        case "function":
                        case "procedure":
                            return Icons.squareFunction;
                        default:
                            return Icons.braces;
                        }
                    }

                    // The icons are decorative: the kind is also said in words.
                    function kindLabel(k) {
                        switch (k) {
                        case "database":
                            return qsTr("Database");
                        case "schema":
                            return qsTr("Schema");
                        case "table":
                            return qsTr("Table");
                        case "view":
                            return qsTr("View");
                        case "materializedView":
                            return qsTr("Materialized view");
                        case "column":
                            return qsTr("Column");
                        case "index":
                            return qsTr("Index");
                        case "foreignKey":
                            return qsTr("Foreign key");
                        case "function":
                            return qsTr("Function");
                        case "procedure":
                            return qsTr("Procedure");
                        case "sequence":
                            return qsTr("Sequence");
                        case "trigger":
                            return qsTr("Trigger");
                        case "error":
                            return qsTr("Error");
                        default:
                            return "";
                        }
                    }

                    function describe() {
                        const parts = [];
                        if (isConnection)
                            parts.push(connected ? qsTr("Connected") : qsTr("Not connected"), summary);
                        else
                            parts.push(kindLabel(kind), detail);
                        parts.push(qsTr("level %1").arg(depth + 1), qsTr("%1 of %2").arg(position).arg(setSize));
                        if (isError)
                            parts.push(qsTr("Press Enter to retry"));
                        return parts.filter(s => s !== "").join(", ");
                    }

                    implicitHeight: Math.max(Theme.controlHeight, content.implicitHeight + Theme.spacing * 2)

                    TableView.onReused: detailsOpen = false

                    Accessible.role: Accessible.TreeItem
                    Accessible.name: isConnection ? [name, providerName, colorName, production ? qsTr("production") : ""].filter(s => s !== "").join(", ") : name
                    Accessible.description: describe()
                    Accessible.expandable: expandable
                    Accessible.expanded: expanded
                    Accessible.selected: current
                    Accessible.focusable: true
                    Accessible.focused: current && node.treeView.activeFocus

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: node.current ? Theme.selectionBg : hover.hovered ? Theme.bgHover : Qt.alpha(Theme.bgSurface, 0)

                        // The current row is marked by a bar as well as by its fill.
                        Rectangle {
                            visible: node.current
                            width: Theme.focusRingWidth
                            height: parent.height
                            color: Theme.accent
                        }
                    }

                    RowLayout {
                        id: content

                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing + node.depth * Theme.spacing * 4
                        anchors.rightMargin: Theme.spacing * 2
                        spacing: Theme.spacing

                        // The expander. Decorative: the expanded state is on the item itself.
                        Item {
                            id: expander

                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: Theme.spacing * 6
                            Layout.preferredHeight: Math.max(Theme.controlHeight - Theme.spacing * 2, Theme.spacing * 6)

                            Icon {
                                anchors.centerIn: parent
                                visible: node.expandable
                                text: node.expanded ? Icons.chevronDown : Icons.chevronRight
                                color: node.mutedColor
                            }
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: Theme.spacing
                            visible: node.hasColor
                            implicitWidth: Theme.spacing * 3
                            implicitHeight: implicitWidth
                            radius: width / 2
                            color: node.hasColor ? Theme.connectionColor(node.colorIndex) : "transparent"
                            border.width: 1
                            border.color: Theme.borderStrong
                        }

                        Icon {
                            id: kindIcon

                            Layout.alignment: Qt.AlignTop
                            Layout.preferredHeight: expander.Layout.preferredHeight
                            visible: node.kind !== "empty"
                            text: node.isLoading ? Icons.loaderCircle : node.isError ? Icons.circleAlert : node.iconFor(node.kind)
                            color: node.isError && !node.current ? Theme.danger : node.mutedColor

                            RotationAnimator on rotation {
                                running: node.isLoading && kindIcon.visible && Theme.animationDuration > 0
                                from: 0
                                to: 360
                                duration: 1000
                                loops: Animation.Infinite
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: 0

                            Text {
                                Layout.fillWidth: true
                                Layout.topMargin: Math.max(0, (expander.Layout.preferredHeight - implicitHeight) / 2)
                                text: node.name
                                font.pointSize: Theme.fontSize
                                font.bold: node.isConnection
                                font.italic: node.kind === "empty"
                                color: node.isError && !node.current ? Theme.danger : node.kind === "empty" || node.isLoading ? node.mutedColor : node.textColor
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                Accessible.ignored: true // the item itself carries name and description
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: node.isConnection ? [node.providerName, node.summary, node.colorName].filter(s => s !== "").join(" · ") : node.isError ? "" : node.detail
                                font.pointSize: Theme.fontSize
                                color: node.mutedColor
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                Accessible.ignored: true
                            }

                            // A connected connection says so in words and with an icon, not by color.
                            RowLayout {
                                visible: node.isConnection && node.connected
                                spacing: Theme.spacing

                                Icon {
                                    text: Icons.plug
                                    color: node.mutedColor
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Connected")
                                    font.pointSize: Theme.fontSize
                                    color: node.mutedColor
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    Accessible.ignored: true
                                }
                            }

                            ProductionBadge {
                                visible: node.isConnection && node.production === true
                            }

                            // Error row: Retry and the collapsible technical details. Enter retries and
                            // Space opens the details from the keyboard; the buttons are for the pointer.
                            RowLayout {
                                visible: node.isError
                                spacing: Theme.spacing

                                QButton {
                                    text: qsTr("Retry")
                                    focusPolicy: Qt.NoFocus
                                    onClicked: {
                                        node.treeView.selectionModel.setCurrentIndex(node.treeView.index(node.row, 0), ItemSelectionModel.ClearAndSelect);
                                        root.connections.tree.retry(node.treeView.index(node.row, 0));
                                    }
                                }

                                QButton {
                                    visible: node.detail !== ""
                                    variant: "flat"
                                    text: qsTr("Technical details")
                                    iconGlyph: node.detailsOpen ? Icons.chevronDown : Icons.chevronRight
                                    focusPolicy: Qt.NoFocus
                                    onClicked: node.toggleDetails()

                                    Accessible.expandable: true
                                    Accessible.expanded: node.detailsOpen
                                }
                            }

                            TextEdit {
                                id: clipboard

                                Layout.fillWidth: true
                                visible: node.isError && node.detailsOpen && node.detail !== ""
                                readOnly: true
                                selectByMouse: true
                                text: node.detail
                                wrapMode: TextEdit.WrapAnywhere
                                font.family: Theme.monoFontFamily
                                font.pointSize: Theme.fontSize
                                color: node.textColor
                                selectionColor: Theme.selectionFg
                                selectedTextColor: Theme.selectionBg

                                Accessible.role: Accessible.StaticText
                                Accessible.name: qsTr("Technical details")
                                Accessible.description: text
                            }
                        }
                    }

                    HoverHandler {
                        id: hover
                    }

                    TapHandler {
                        onTapped: point => {
                            node.treeView.selectionModel.setCurrentIndex(node.treeView.index(node.row, 0), ItemSelectionModel.ClearAndSelect);
                            node.treeView.forceActiveFocus(Qt.MouseFocusReason);
                            if (node.expandable && point.position.x < expander.x + expander.width + content.anchors.leftMargin)
                                // qmllint disable missing-property
                                root.tree.toggleRow(node.row);
                                // qmllint enable missing-property
                        }
                        onDoubleTapped: point => {
                            if (node.expandable && point.position.x >= expander.x + expander.width + content.anchors.leftMargin)
                                // qmllint disable missing-property
                                root.tree.toggleRow(node.row);
                                // qmllint enable missing-property
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            const idx = node.treeView.index(node.row, 0);
                            node.treeView.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect);
                            node.treeView.forceActiveFocus(Qt.MouseFocusReason);
                            root.popupMenu(node, idx);
                        }
                    }
                }

                FocusRing {
                    parent: treeView // not the flickable's content item
                    shown: treeView.activeFocus
                    inset: true
                }
            }
        }
    }

    Loader {
        id: passwordLoader

        active: false
        sourceComponent: PasswordDialog {
            onSubmitted: (id, password) => root.connections.tree.providePassword(id, password)
            onDismissed: id => root.connections.tree.cancelPassword(id)
        }
    }

    QMenu {
        id: actionMenu

        // The chosen action runs once the menu has closed, so the dialog it opens remembers the
        // tree (not the menu) as the place to return the focus to. The actions are about the
        // connection the node belongs to.
        property string targetId: ""
        property string action: ""

        title: qsTr("Connection actions")
        onClosed: {
            const id = targetId;
            const what = action;
            action = "";
            if (what !== "")
                Qt.callLater(() => {
                    if (what === "edit")
                        root.editRequested(id);
                    else if (what === "duplicate")
                        root.duplicateRequested(id);
                    else if (what === "disconnect")
                        root.connections.tree.disconnectConnection(id);
                    else
                        root.deleteRequested(id);
                });
        }

        QMenuItem {
            text: qsTr("Edit")
            onTriggered: actionMenu.action = "edit"
        }

        QMenuItem {
            text: qsTr("Duplicate")
            onTriggered: actionMenu.action = "duplicate"
        }

        QMenuItem {
            text: qsTr("Disconnect")
            enabled: root.connections.tree.connectedIds.includes(actionMenu.targetId)
            onTriggered: actionMenu.action = "disconnect"
        }

        QMenuItem {
            text: qsTr("Delete")
            onTriggered: actionMenu.action = "delete"
        }
    }
}
