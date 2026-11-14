import QtQuick
import QtTest
import Quarry.Controls

// The on-demand schema tree (H1.4) in the sidebar, driven the way a keyboard-only user would:
// the real SQLite plugin over a small sample database in a temporary directory. No test saves a
// password, so the system keychain is never touched.
TestCase {
    id: testCase

    property var shell: null
    property int fileCounter: 0

    name: "SchemaTree"
    when: windowShown
    width: 900
    height: 700
    visible: true

    A11y {
        id: walker
    }

    Component {
        id: spyComp

        SignalSpy {}
    }

    // ---- helpers -----------------------------------------------------------------------------

    function tree() {
        return shell.sidebar.tree;
    }

    function model() {
        return shell.connections.model;
    }

    function typeText(text) {
        for (const ch of text)
            keyClick(ch);
    }

    function rowName(row) {
        return tree().roleAt(row, SchemaTreeModel.NameRole);
    }

    function rowKind(row) {
        return tree().roleAt(row, SchemaTreeModel.KindRole);
    }

    function currentName() {
        return rowName(tree().cursorRow());
    }

    function itemAt(row) {
        return tree().itemAtCell(0, row);
    }

    function find(root, pred) {
        return walker.findAll(root, i => i.visible && pred(i))[0];
    }

    function announcements(spy) {
        return spy.signalArguments.map(a => a[0]);
    }

    function addSqlite(path) {
        const spy = createTemporaryObject(spyComp, testCase, {
            target: shell.connections,
            signalName: "connectionSaved"
        });
        shell.connections.openSqliteFile(Qt.url("file:///" + path));
        tryCompare(spy, "count", 1);
        return spy.signalArguments[0][0];
    }

    function removeAll() {
        for (let i = model().count - 1; i >= 0; --i)
            shell.connections.remove(model().get(i).connectionId);
        tryCompare(model(), "count", 0);
    }

    function closeOverlays() {
        const d = shell.sidebar.passwordDialog;
        if (d && d.visible) {
            tryVerify(() => d.opened || !d.visible);
            d.close();
        }
        tryVerify(() => !(shell.sidebar.passwordDialog?.visible) && !(shell.connectionDialog?.visible) && !(shell.deleteDialog?.visible), 5000);
    }

    function initTestCase() {
        const comp = Qt.createComponent(Qt.resolvedUrl("../../src/app/qml/MainWindow.qml"));
        compare(comp.status, Component.Ready, comp.errorString());
        shell = comp.createObject(testCase, {
            width: testCase.width,
            height: testCase.height,
            databasePath: testTempDir + "/schematree.db",
            providerDirectory: testPluginDir
        });
        verify(shell !== null);
        tryVerify(() => !model().loading, 10000);
    }

    function init() {
        closeOverlays();
        Language.choice = Language.Choice.English;
        removeAll();
        shell.focusRegion(1);
    }

    // ---- accessibility -----------------------------------------------------------------------

    function test_emptyTreeWalker() {
        compare(model().count, 0);
        verify(find(shell.sidebar, i => i.title === "No connections yet") !== undefined);
        compare(walker.problems(shell.sidebar), []);
        compare(CommandRegistry.find("connection.disconnect").enabled, false);
    }

    function test_expandedTreeIsLabelledAndHasLevelAndPosition() {
        addSqlite(testSampleDb);
        shell.focusRegion(0);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 2 && rowName(1) === "main", 10000);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Right); // database -> folders
        tryVerify(() => tree().rows === 4);
        keyClick(Qt.Key_Right); // first child: Tables
        keyClick(Qt.Key_Right); // expand Tables
        tryVerify(() => tree().rows === 8);
        tryVerify(() => itemAt(2)?.Accessible.name === "Tables" && itemAt(4)?.Accessible.name === "invoices" && itemAt(0)?.Accessible.description.startsWith("Connected"));
        compare(walker.problems(shell), []);

        const tables = itemAt(2);
        compare(tables.Accessible.role, Accessible.TreeItem);
        compare(tables.Accessible.name, "Tables");
        compare(tables.Accessible.expandable, true);
        compare(tables.Accessible.expanded, true);
        compare(tables.Accessible.description, "level 3, 1 of 2");
        const invoices = itemAt(4);
        compare(invoices.Accessible.name, "invoices");
        compare(invoices.Accessible.description, "Table, level 4, 2 of 4");
        verify(!invoices.Accessible.expanded);
        compare(tree().Accessible.role, Accessible.Tree);
        // the connection says it is connected in words, and shows its icon and text
        const root = itemAt(0);
        verify(root.Accessible.description.startsWith("Connected"));
        verify(find(root, i => i.text === "Connected") !== undefined);
    }

    // What the platform accessibility bridge sees (QAccessibleInterface), not only the QML properties.
    function test_accessibilityInterfaceOfTheRows() {
        addSqlite(testSampleDb);
        shell.focusRegion(0);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 2 && rowName(1) === "main", 10000);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 4);
        keyClick(Qt.Key_Right); // Tables
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 8);

        tryVerify(() => itemAt(2)?.Accessible.name === "Tables" && itemAt(7)?.Accessible.name === "Views" && itemAt(4)?.Accessible.name === "invoices");
        const treeInfo = a11yProbe.inspect(tree());
        compare(treeInfo.role, 0x23, "Tree"); // QAccessible::Tree
        compare(treeInfo.name, "Connections");
        const tables = a11yProbe.inspect(itemAt(2));
        compare(tables.role, 0x24, "TreeItem"); // QAccessible::TreeItem
        compare(tables.name, "Tables");
        compare(tables.expandable, true);
        compare(tables.expanded, true);
        compare(tables.description, "level 3, 1 of 2");
        const views = a11yProbe.inspect(itemAt(7));
        compare(views.name, "Views");
        compare(views.expandable, true);
        compare(views.expanded, false);
        const leaf = a11yProbe.inspect(itemAt(4));
        compare(leaf.name, "invoices");
        compare(leaf.expanded, false);
        compare(leaf.description, "Table, level 4, 2 of 4");
        tryVerify(() => a11yProbe.inspect(itemAt(tree().cursorRow())).selected);
        tryVerify(() => a11yProbe.inspect(itemAt(tree().cursorRow())).focused);
    }

    function test_connectionErrorRowIsFocusableAndRetryable() {
        // A directory where the database file should be: opening it fails.
        addSqlite(testTempDir);
        const spy = createTemporaryObject(spyComp, testCase, {
            target: shell.connections.tree,
            signalName: "announcement"
        });
        shell.focusRegion(0);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 2 && rowKind(1) === "error", 10000);
        verify(announcements(spy).includes("Loading…"));
        const errorAnnouncement = spy.signalArguments.find(a => a[0] !== "Loading…");
        compare(errorAnnouncement[1], true, "errors are assertive");
        compare(errorAnnouncement[0], rowName(1));
        compare(CommandRegistry.find("connection.disconnect").enabled, false);

        keyClick(Qt.Key_Down); // the error row is reachable and current
        compare(tree().cursorRow(), 1);
        // Delegates are recycled when rows change: always ask for the current one.
        tryVerify(() => itemAt(1) !== null);
        compare(itemAt(1).Accessible.role, Accessible.TreeItem);
        tryVerify(() => itemAt(1)?.Accessible.description.includes("Press Enter to retry"));
        tryVerify(() => itemAt(1)?.Accessible.focused);
        compare(walker.problems(shell), []);

        // Technical details start collapsed; Space opens them.
        verify(!itemAt(1).detailsOpen);
        keyClick(Qt.Key_Space);
        verify(itemAt(1).detailsOpen);
        verify(find(itemAt(1), i => i.text === tree().roleAt(1, SchemaTreeModel.DetailRole) && i.readOnly === true) !== undefined);
        compare(walker.problems(shell), []);
        keyClick(Qt.Key_Space);
        verify(!itemAt(1).detailsOpen);

        // Enter retries.
        const loadingBefore = announcements(spy).filter(t => t === "Loading…").length;
        keyClick(Qt.Key_Return);
        tryVerify(() => announcements(spy).filter(t => t === "Loading…").length > loadingBefore);
        tryVerify(() => tree().rows === 2 && rowKind(1) === "error", 10000);

        // The Retry button does the same for the pointer.
        const again = announcements(spy).filter(t => t === "Loading…").length;
        wait(200); // the new error row finishes its layout before the pointer aims at it
        const retry = find(tree(), i => i.text === "Retry" && i.clicked !== undefined);
        verify(retry !== undefined);
        compare(retry.Accessible.role, Accessible.Button);
        mouseClick(retry);
        tryVerify(() => announcements(spy).filter(t => t === "Loading…").length > again);
        tryVerify(() => tree().rows === 2 && rowKind(1) === "error", 10000);
    }

    function test_passwordDialog() {
        const id = addSqlite(testSampleDb);
        shell.focusRegion(0);
        const t = shell.connections.tree;
        t.passwordRequested(id, "Sample", false);
        tryVerify(() => shell.sidebar.passwordDialog?.opened);
        const dlg = shell.sidebar.passwordDialog;
        tryVerify(() => dlg.passwordField.activeFocus);
        compare(dlg.passwordField.Accessible.role, Accessible.EditableText);
        compare(dlg.passwordField.Accessible.passwordEdit, true);
        compare(dlg.passwordField.echoMode, TextInput.Password);
        compare(walker.problems(shell.overlayItem), []);
        verify(find(shell.overlayItem, i => i.text === "The password was not accepted. Try again.") === undefined);
        typeText("secret");
        keyClick(Qt.Key_Return);
        tryVerify(() => !dlg.visible);
        compare(dlg.passwordField.text, "", "the password does not linger in the field");
        tryVerify(() => tree().activeFocus, 5000, "focus returns to the tree");

        // Again, after a refused password: the dialog says so with text and icon. Esc dismisses.
        t.passwordRequested(id, "Sample", true);
        tryVerify(() => dlg.opened);
        verify(find(shell.overlayItem, i => i.text === "The password was not accepted. Try again.") !== undefined);
        compare(walker.problems(shell.overlayItem), []);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dlg.visible);
        tryVerify(() => tree().activeFocus);
    }

    // ---- the keyboard-only flow --------------------------------------------------------------

    function test_keyboardOnlyFlow() {
        const spy = createTemporaryObject(spyComp, testCase, {
            target: shell.connections.tree,
            signalName: "announcement"
        });
        const id = addSqlite(testSampleDb);
        shell.focusRegion(0);
        verify(tree().activeFocus);
        compare(tree().cursorRow(), 0);
        compare(CommandRegistry.find("connection.disconnect").enabled, false);
        verify(find(shell.regions[3], i => i.text === "Not connected") !== undefined);

        // Right expands the connection and connects: "Loading…" is announced, then the rows.
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 2 && rowName(1) === "main", 10000);
        verify(announcements(spy).includes("Loading…"));
        verify(announcements(spy).includes("Connected to sample.db"));
        tryVerify(() => CommandRegistry.find("connection.disconnect").enabled);
        tryVerify(() => find(shell.regions[3], i => i.text === "Connected") !== undefined);
        compare(shell.sidebar.currentConnected, true);

        // Right again goes to the first child; Down/Up move.
        keyClick(Qt.Key_Right);
        compare(currentName(), "main");
        keyClick(Qt.Key_Right); // expand the database
        tryVerify(() => tree().rows === 4);
        compare(currentName(), "main");
        keyClick(Qt.Key_Down);
        compare(currentName(), "Tables");
        keyClick(Qt.Key_Up);
        compare(currentName(), "main");
        keyClick(Qt.Key_Right);
        compare(currentName(), "Tables");
        keyClick(Qt.Key_Right); // expand Tables
        tryVerify(() => tree().rows === 8);
        keyClick(Qt.Key_Right);
        compare(currentName(), "customers");

        // Type-ahead: the next node starting with the letters; typing on narrows; no filtering.
        typeText("i");
        compare(currentName(), "invoices");
        typeText("t");
        compare(currentName(), "items");
        compare(tree().rows, 8);
        wait(1100); // the prefix resets after about a second
        typeText("p");
        compare(currentName(), "products");
        typeText("c"); // "pc" matches nothing: stays
        compare(currentName(), "products");
        wait(1100);
        typeText("c"); // wraps around to the start of the list
        compare(currentName(), "customers");

        // Home/End, Left to the parent, Left collapses, Enter toggles.
        keyClick(Qt.Key_End);
        compare(tree().cursorRow(), 7);
        compare(currentName(), "Views");
        keyClick(Qt.Key_Home);
        compare(tree().cursorRow(), 0);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(currentName(), "customers");
        keyClick(Qt.Key_Left);
        compare(currentName(), "Tables");
        keyClick(Qt.Key_Left);
        compare(currentName(), "Tables");
        tryCompare(tree(), "rows", 4); // collapsed
        keyClick(Qt.Key_Return);
        tryCompare(tree(), "rows", 8); // already loaded: no new request
        keyClick(Qt.Key_Return);
        tryCompare(tree(), "rows", 4);
        keyClick(Qt.Key_Return);
        tryCompare(tree(), "rows", 8);

        // Collapsing the connection does not disconnect it.
        keyClick(Qt.Key_Home);
        keyClick(Qt.Key_Left);
        tryCompare(tree(), "rows", 1);
        compare(shell.connections.tree.connectedIds.length, 1);
        compare(CommandRegistry.find("connection.disconnect").enabled, true);
        keyClick(Qt.Key_Right); // expands again from the loaded subtree, without reconnecting
        tryCompare(tree(), "rows", 8);

        // Shift+F10 opens the context menu on any node; Esc returns the focus.
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_F10, Qt.ShiftModifier);
        tryVerify(() => shell.sidebar.menu.opened);
        compare(shell.sidebar.menu.itemAt(2).text, "Disconnect");
        compare(shell.sidebar.menu.itemAt(2).enabled, true);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.sidebar.menu.visible);
        tryVerify(() => tree().activeFocus);

        // Disconnect from the menu: the subtree is dropped, the node collapses, the focus stays.
        keyClick(Qt.Key_F10, Qt.ShiftModifier);
        tryVerify(() => shell.sidebar.menu.opened);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        tryCompare(shell.connections.tree.connectedIds, "length", 0);
        tryCompare(tree(), "rows", 1);
        verify(announcements(spy).some(t => t.startsWith("Disconnected from")));
        compare(CommandRegistry.find("connection.disconnect").enabled, false);
        tryVerify(() => tree().activeFocus);
        verify(!itemAt(0).Accessible.expanded);

        // Expanding connects again; the command (palette, no shortcut) disconnects.
        shell.sidebar.selectId(id);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 2 && rowName(1) === "main", 10000);
        CommandRegistry.trigger("connection.disconnect");
        tryCompare(tree(), "rows", 1);
        compare(shell.connections.tree.connectedIds.length, 0);
    }

    function test_f6AndModOneLandOnTheTree() {
        addSqlite(testSampleDb);
        keyClick(Qt.Key_1, Qt.ControlModifier);
        verify(tree().activeFocus);
        keyClick(Qt.Key_F6);
        verify(!tree().activeFocus);
        keyClick(Qt.Key_F6, Qt.ShiftModifier);
        verify(tree().activeFocus);
        verify(walker.findAll(tree(), i => i.objectName === "focusRing" && i.visible).length === 1);
        keyClick(Qt.Key_2, Qt.ControlModifier);
        shell.focusRegion(0);
        verify(tree().activeFocus);
    }

    // 10 000 tables under one folder, through the real TreeView: the event loop keeps turning.
    function test_tenThousandTablesKeepTheUiResponsive() {
        // The idle preload of the palette and settings (1.5 s after start) is not what is measured.
        tryVerify(() => shell.paletteDialog && shell.settingsDialog, 10000);
        wait(200);
        addSqlite(testBigDb);
        shell.focusRegion(0);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 2 && rowName(1) === "main", 20000);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 4, 20000);
        keyClick(Qt.Key_Right); // Tables
        // Idle floor of this harness, then the expand itself (10 004 children: the sample's four
        // plus 10 000, then Views).
        ticker.last = Date.now();
        ticker.maxGap = 0;
        ticker.running = true;
        wait(700);
        console.log("IDLE max event-loop gap " + ticker.maxGap + " ms");
        // Three cold expands (disconnect in between, so each one fetches and builds the rows
        // again): the median is asserted, because single 24-36 ms spikes inside Qt Quick come and
        // go with machine load (same rule as the startup bench, PERFORMANCE.md).
        const gaps = [];
        for (let i = 0; i < 3; ++i) {
            if (i > 0) {
                CommandRegistry.trigger("connection.disconnect");
                tryCompare(tree(), "rows", 1, 20000);
                keyClick(Qt.Key_Home);
                keyClick(Qt.Key_Right);
                tryVerify(() => tree().rows === 2 && rowName(1) === "main", 20000);
                keyClick(Qt.Key_Down);
                keyClick(Qt.Key_Right);
                tryVerify(() => tree().rows === 4, 20000);
                keyClick(Qt.Key_Right); // Tables
                wait(300);
            }
            ticker.maxGap = 0;
            ticker.last = Date.now();
            keyClick(Qt.Key_Right);
            tryVerify(() => tree().rows === 10008, 60000);
            wait(100);
            gaps.push(ticker.maxGap);
        }
        ticker.running = false;
        const median = gaps.slice().sort((a, b) => a - b)[1];
        console.log("TREE10K max event-loop gaps " + gaps.join(", ") + " ms, median " + median);
        verify(median <= testMaxGapMs, "event loop blocked for " + median + " ms (median of " + gaps.join(", ") + "; limit " + testMaxGapMs + ")");
        keyClick(Qt.Key_End);
        compare(rowName(tree().cursorRow()), "Views");
        keyClick(Qt.Key_Home);
        compare(tree().cursorRow(), 0);
    }

    Timer {
        id: ticker

        property double last: 0
        property double maxGap: 0

        interval: 5
        repeat: true
        onTriggered: {
            const now = Date.now();
            maxGap = Math.max(maxGap, now - last);
            last = now;
        }
    }

    function test_spanishTree() {
        addSqlite(testSampleDb);
        shell.focusRegion(0);
        Language.choice = Language.Choice.Spanish;
        tryVerify(() => itemAt(0)?.Accessible.description.startsWith("Sin conexión, sample.db, nivel 1, 1 de 1"));
        compare(CommandRegistry.find("connection.disconnect").title, "Desconectar");
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 2 && rowName(1) === "main", 10000);
        tryVerify(() => itemAt(0).Accessible.description.startsWith("Conectada"));
        tryVerify(() => find(shell.regions[3], i => i.text === "Conectada") !== undefined);
        keyClick(Qt.Key_Down);
        // The delegates catch up with the model on the next layout pass.
        tryVerify(() => itemAt(1)?.Accessible.description.startsWith("Base de datos, "));
        verify(itemAt(1).Accessible.description.endsWith(", nivel 2, 1 de 1"));
        compare(walker.problems(shell), []);
    }

    function test_removingAConnectedConnectionKeepsTheTreeConsistent() {
        const id = addSqlite(testSampleDb);
        shell.focusRegion(0);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree().rows === 2 && rowName(1) === "main", 10000);
        shell.connections.remove(id);
        tryCompare(model(), "count", 0);
        tryVerify(() => shell.sidebar.tree === null, 5000, "the tree is gone with its last connection");
        compare(shell.connections.tree.connectedIds.length, 0);
        verify(shell.sidebar.defaultFocus === shell.sidebar.newButton);
    }
}
