import QtQuick
import QtTest
import Quarry.Controls

// Running queries from the editor (H1.6): the UI keeps answering while a slow query runs, the
// start and the end are announced without taking the focus, the SqlSafety confirmation is a
// keyboard flow, Esc and Mod+. cancel, and the results region, the error underline and the dialog
// pass the accessibility walker. Temporary databases and the real SQLite plugin; no keychain.
TestCase {
    id: testCase

    property var shell: null
    property int fileCounter: 0
    // What QueryController sent to the screen reader: {text, assertive}.
    property var said: []

    // Runs for seconds and is interruptible.
    readonly property string slowSql: "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x < 100000000000) SELECT count(*) FROM c"

    name: "Query"
    when: windowShown
    width: 1000
    height: 700
    visible: true

    A11y {
        id: walker
    }

    Component {
        id: spyComp

        SignalSpy {}
    }

    Connections {
        target: testCase.shell ? testCase.shell.queries : null

        function onAnnouncement(text, assertive) {
            testCase.said.push({
                "text": text,
                "assertive": assertive
            });
        }
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

    // ---- helpers -----------------------------------------------------------------------------

    function tabs() {
        return shell.editorTabs;
    }

    function editor() {
        return shell.editorArea.currentEditor;
    }

    function model() {
        return shell.connections.model;
    }

    function overlay() {
        return shell.overlayItem;
    }

    function current() {
        return shell.queries.current;
    }

    function find(root, pred) {
        return walker.findAll(root, i => i.visible && pred(i))[0];
    }

    function texts() {
        return said.map(a => a.text);
    }

    function newFile() {
        return testTempDir + "/query-data-" + (++fileCounter) + ".db";
    }

    function typeText(text) {
        for (const ch of text) {
            if (ch === " ")
                keyClick(Qt.Key_Space);
            else
                keyClick(ch);
        }
    }

    function waitForViews() {
        tryVerify(() => shell.sidebar.tree === null ? model().count === 0 : shell.sidebar.tree.rows >= model().count);
        if (tabs().count > 0)
            tryVerify(() => editor() !== null && editor().visible);
    }

    function addSqlite(file) {
        const spy = createTemporaryObject(spyComp, testCase, {
            target: shell.connections,
            signalName: "connectionSaved"
        });
        shell.connections.openSqliteFile(Qt.url("file:///" + file));
        tryCompare(spy, "count", 1);
        waitForViews();
        return spy.signalArguments[0][0];
    }

    // A tab on the connection of the tree node, with `sql` as its text; the editor has the focus.
    function openQuery(sql) {
        shell.focusRegion(1);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        tryVerify(() => editor().edit.activeFocus);
        editor().edit.text = sql;
        compare(tabs().textOf(tabs().currentIndex), sql);
    }

    function removeAll() {
        while (tabs().count > 0)
            tabs().closeTab(0);
        for (let i = model().count - 1; i >= 0; --i)
            shell.connections.remove(model().get(i).connectionId);
        tryCompare(model(), "count", 0);
    }

    function closeOverlays() {
        for (const d of [shell.confirmRunDialog, shell.closeTabDialog, shell.connectionDialog, shell.deleteDialog, shell.paletteDialog]) {
            if (d && d.visible) {
                tryVerify(() => d.opened || !d.visible);
                d.close();
            }
        }
        tryVerify(() => ![shell.confirmRunDialog, shell.closeTabDialog, shell.connectionDialog, shell.deleteDialog, shell.paletteDialog].some(d => d && d.visible), 5000);
    }

    function initTestCase() {
        const comp = Qt.createComponent(Qt.resolvedUrl("../../src/app/qml/MainWindow.qml"));
        compare(comp.status, Component.Ready, comp.errorString());
        shell = comp.createObject(testCase, {
            width: testCase.width,
            height: testCase.height,
            databasePath: testTempDir + "/query.db",
            providerDirectory: testPluginDir
        });
        verify(shell !== null);
        tryVerify(() => !model().loading, 10000);
        tryVerify(() => tabs().loaded, 10000);
    }

    function init() {
        closeOverlays();
        Language.choice = Language.Choice.English;
        Theme.resetZoom();
        shell.resetLayout();
        ticker.running = false;
        removeAll();
        said.length = 0;
        shell.resultsPanel.tabBar.currentIndex = 0; // an error leaves Messages shown
        shell.focusRegion(1);
    }

    function cleanupTestCase() {
        Language.choice = Language.Choice.English;
    }

    // ---- the ROADMAP criterion ---------------------------------------------------------------

    // While the slow query runs: switch tabs, expand the tree and cancel. The event loop keeps
    // turning (the gap budget of the schema tree test; a debug build only has to stay responsive).
    function test_theUiRespondsWhileAQueryRuns() {
        // The idle preload of the palette and settings (1.5 s after start) is not what is measured.
        tryVerify(() => shell.paletteDialog && shell.settingsDialog, 10000);
        wait(200);
        addSqlite(newFile());
        shell.focusRegion(0);
        openQuery("SELECT 1");
        openQuery(slowSql);
        compare(tabs().count, 2);
        const slowTab = tabs().idOf(1);

        ticker.last = Date.now();
        ticker.maxGap = 0;
        ticker.running = true;
        wait(700);
        console.log("IDLE max event-loop gap " + ticker.maxGap + " ms");
        // A machine that already stalls this long doing nothing cannot measure the budget.
        if (ticker.maxGap > testMaxGapMs / 2) {
            ticker.running = false;
            skip("idle event-loop gap " + ticker.maxGap + " ms: too noisy to measure a " + testMaxGapMs + " ms budget");
        }

        const gaps = [];
        for (let i = 0; i < 3; ++i) {
            ticker.maxGap = 0;
            ticker.last = Date.now();
            keyClick(Qt.Key_Return, Qt.ControlModifier);
            tryCompare(current(), "running", true, 5000);
            // Another tab: it is idle, the status bar follows it.
            keyClick(Qt.Key_Tab, Qt.ControlModifier);
            compare(tabs().currentIndex, 0);
            tryVerify(() => !current().running);
            tryVerify(() => !find(shell.statusItem, i => i.Accessible.name === "Cancel query"));
            // The tree: expand and collapse the connection while the query runs.
            shell.focusRegion(0);
            keyClick(Qt.Key_Home);
            keyClick(Qt.Key_Right);
            tryVerify(() => shell.sidebar.tree.rows >= 2, 20000);
            keyClick(Qt.Key_Left);
            tryVerify(() => shell.sidebar.tree.rows === 1, 20000);
            // Back to the running tab and cancel it from the keyboard.
            keyClick(Qt.Key_Tab, Qt.ControlModifier);
            compare(tabs().currentIndex, 1);
            verify(current().running, "the query is still running");
            keyClick(Qt.Key_Period, Qt.ControlModifier);
            tryCompare(current(), "state", "cancelled", 20000);
            wait(50);
            gaps.push(ticker.maxGap);
            keyClick(Qt.Key_Escape); // nothing is running: Esc leaves the state alone
            compare(current().state, "cancelled");
            compare(shell.queries.stateOf(slowTab), current());
        }
        ticker.running = false;
        const median = gaps.slice().sort((a, b) => a - b)[1];
        console.log("QUERY max event-loop gaps " + gaps.join(", ") + " ms, median " + median);
        verify(median <= testMaxGapMs, "event loop blocked for " + median + " ms (median of " + gaps.join(", ") + "; limit " + testMaxGapMs + ")");
    }

    // ---- announcements -----------------------------------------------------------------------

    function test_startAndEndAreAnnouncedWithoutMovingTheFocus() {
        addSqlite(newFile());
        openQuery("SELECT 1 AS one");
        const focus = testCase.Window.activeFocusItem;
        verify(focus === editor().edit);
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(current(), "state", "done");
        tryVerify(() => said.length >= 2);
        compare(said[0].text, "Running statement 1 of 1");
        verify(!said[0].assertive, "the start is polite");
        verify(said[1].text.startsWith("Done: 1 row in "), said[1].text);
        verify(!said[1].assertive, "the end is polite");
        verify(testCase.Window.activeFocusItem === focus, "the focus did not move");
        verify(shell.editorArea.lastAnnouncement.startsWith("Done: 1 row in "));
        // The status bar says the same, in words.
        verify(current().statusText.startsWith("1 row · "), current().statusText);
        verify(shell.statusItem.Accessible.name.startsWith("1 row · "));

        // An error is assertive, goes to Messages, and still does not take the focus.
        said.length = 0;
        editor().edit.text = "SELECT * FROM missing_table";
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(current(), "state", "failed");
        tryVerify(() => said.some(a => a.assertive));
        verify(said.find(a => a.assertive).text.length > 0);
        compare(shell.resultsPanel.tabBar.currentIndex, 1);
        verify(testCase.Window.activeFocusItem === focus, "the focus did not move on an error");
        compare(current().statusText, "Query failed");
    }

    // ---- confirmation ------------------------------------------------------------------------

    function test_keyboardOnlyConfirmationFlow() {
        addSqlite(newFile());
        openQuery("CREATE TABLE t(a); INSERT INTO t VALUES (1),(2)");
        keyClick(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier);
        tryCompare(current(), "state", "done");

        // DELETE without WHERE: the dialog first.
        editor().edit.text = "";
        typeText("DELETE FROM t");
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryVerify(() => shell.confirmRunDialog?.opened);
        const dialog = shell.confirmRunDialog;
        verify(dialog.cancelButton.activeFocus, "the focus starts on Cancel");
        verify(dialog.runButton.variant === "danger");
        compare(dialog.runButton.text, "Run anyway");
        verify(dialog.items.length === 1);
        compare(dialog.items[0].risk, "DELETE without WHERE");
        compare(dialog.connectionName, model().get(0).name);
        verify(find(dialog.contentItem, i => i.text === "DELETE without WHERE" || (i.text && i.text.endsWith("DELETE without WHERE"))) !== undefined);
        // Esc: the whole plan is cancelled before it starts, and the focus returns to the editor.
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        tryVerify(() => editor().edit.activeFocus, 5000, "the focus returns to the editor");
        compare(current().state, "done"); // still the CREATE/INSERT run: nothing ran
        wait(200);
        compare(current().state, "done");
        compare(current().messages.count, 2);
        verify(texts().some(t => t.startsWith("Cancelled")));

        // Again, and now the explicit button.
        said.length = 0;
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryVerify(() => dialog.opened);
        keyClick(Qt.Key_Tab); // Cancel -> Run anyway
        verify(dialog.runButton.activeFocus);
        keyClick(Qt.Key_Space);
        tryVerify(() => !dialog.visible);
        tryCompare(current(), "state", "done");
        compare(current().affectedRows, 2);
        verify(current().statusText.startsWith("2 rows affected · "), current().statusText);
        tryVerify(() => editor().edit.activeFocus, 5000, "the focus is in the editor");
        compare(current().messages.count, 1);
    }

    function test_productionDropAsksToo() {
        shell.connections.prepare("");
        shell.connections.form.setValue(0, newFile());
        const spy = createTemporaryObject(spyComp, testCase, {
            target: shell.connections,
            signalName: "connectionSaved"
        });
        verify(shell.connections.save({
            "id": "",
            "providerId": "sqlite",
            "name": "Prod shop",
            "colorIndex": 3,
            "production": true,
            "passwordMode": 1,
            "password": "",
            "passwordTouched": false
        }));
        tryCompare(spy, "count", 1);
        waitForViews();
        openQuery("DROP TABLE IF EXISTS nothing");
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryVerify(() => shell.confirmRunDialog?.opened);
        const dialog = shell.confirmRunDialog;
        compare(dialog.items[0].risk, "DROP on a production connection");
        verify(dialog.production);
        verify(find(dialog.contentItem, i => i.text === "PRODUCTION") !== undefined, "the PRODUCTION mark is in the dialog");
        compare(walker.problems(overlay()), []);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        compare(current().state, "idle");
    }

    // ---- cancel ------------------------------------------------------------------------------

    function test_escAndModPeriodCancelARunningQuery() {
        addSqlite(newFile());
        openQuery(slowSql);
        const cancelCmd = CommandRegistry.find("query.cancel");
        verify(cancelCmd !== null);
        compare(cancelCmd.title, "Cancel query");
        compare(cancelCmd.shortcutText, CommandRegistry.displayShortcut("Ctrl+."));
        verify(!cancelCmd.enabled, "nothing runs: disabled");

        // Esc in the editor.
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(current(), "running", true);
        verify(cancelCmd.enabled);
        verify(editor().edit.activeFocus);
        keyClick(Qt.Key_Escape);
        tryCompare(current(), "state", "cancelled", 20000);
        verify(!cancelCmd.enabled);
        compare(current().statusText, "Query cancelled");
        verify(current().messages.count === 1);
        compare(current().messages.data(current().messages.index(0, 0), Qt.UserRole + 1), "cancelled"); // KindRole

        // Esc in the results region.
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(current(), "running", true);
        shell.focusRegion(2);
        verify(shell.regions[2].defaultFocus.activeFocus);
        keyClick(Qt.Key_Escape);
        tryCompare(current(), "state", "cancelled", 20000);

        // Mod+. from anywhere, here the sidebar.
        shell.focusRegion(1);
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(current(), "running", true);
        shell.focusRegion(0);
        keyClick(Qt.Key_Period, Qt.ControlModifier);
        tryCompare(current(), "state", "cancelled", 20000);

        // The palette lists it, and the status bar button does the same.
        shell.focusRegion(1);
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(current(), "running", true);
        const button = find(shell.statusItem.parent, i => i.Accessible.name === "Cancel query");
        verify(button !== undefined, "the status bar has a Cancel button while running");
        verify(current().statusText.startsWith("Running… 00:0"));
        verify(shell.statusItem.Accessible.name.startsWith("Running… 00:0"));
        button.clicked();
        tryCompare(current(), "state", "cancelled", 20000);
        tryVerify(() => !find(shell.statusItem.parent, i => i.Accessible.name === "Cancel query"));
        verify(!said.some(a => a.assertive), "a cancel is never announced as an error");
    }

    // ---- results, messages and the underline -------------------------------------------------

    function test_resultsMessagesAndTheErrorUnderline() {
        addSqlite(newFile());
        const panel = shell.resultsPanel;
        compare(panel.resultsEmpty.message, "Run a query to see results here.");
        compare(panel.messagesEmpty.title, "No messages.");

        openQuery("CREATE TABLE t(a, b); INSERT INTO t VALUES (1, 2), (3, 4); SELECT a, b FROM t");
        keyClick(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier);
        tryCompare(current(), "state", "done");
        verify(panel.summaryItem.visible);
        verify(find(panel.summaryItem, i => i.text === "Columns: a, b") !== undefined);
        verify(find(panel.summaryItem, i => i.text && i.text.startsWith("2 rows · ")) !== undefined);
        panel.tabBar.currentIndex = 1;
        tryCompare(panel.messageList, "count", 3);
        verify(find(panel.messageList, i => i.text === "Statement 2 of 3: 2 rows affected") !== undefined);
        compare(walker.problems(panel), []);

        // An error with a position: Messages, icon and type in words, details, and the underline.
        panel.tabBar.currentIndex = 0;
        editor().edit.text = "SELECT 1;\nSELECT * frm t;";
        said.length = 0;
        keyClick(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier);
        tryCompare(current(), "state", "failed");
        compare(panel.tabBar.currentIndex, 1);
        compare(current().errorLine, 2);
        tryVerify(() => said.some(a => a.assertive && a.text.includes("Error at line 2, column " + current().errorColumn)), 5000, JSON.stringify(said));
        verify(current().errorOffset >= 10);
        const underline = find(editor(), i => i.objectName === "errorUnderline");
        verify(underline !== undefined && underline.visible, "the error is underlined");
        verify(underline.width > 0 && underline.height > 0);
        const marker = walker.findAll(editor(), i => i.objectName === "errorMarker")[0];
        compare(marker.opacity, 1); // the gutter marker, not only a color
        const rect = editor().edit.positionToRectangle(current().errorOffset);
        compare(Math.round(underline.x), Math.round(rect.x));
        verify(underline.y >= rect.y, "below the glyphs of the line");

        tryCompare(panel.messageList, "count", 2);
        tryVerify(() => find(panel.messageList, i => i.text === "Syntax error") !== undefined || find(panel.messageList, i => i.title !== undefined && i.title !== "") !== undefined);
        const details = find(panel.messageList, i => i.text === "Technical details" && i.clicked !== undefined);
        verify(details !== undefined, "the driver text is in a collapsible section");
        compare(details.Accessible.expandable, true);
        compare(details.Accessible.expanded, false);
        details.clicked();
        tryVerify(() => details.Accessible.expanded);
        const detailText = find(panel.messageList, i => i.Accessible.name === "Technical details" && i.readOnly === true);
        verify(detailText !== undefined && detailText.text.length > 0);
        verify(find(panel.messageList, i => i.text === "Copy details") !== undefined);
        compare(walker.problems(panel), []);
        compare(walker.problems(shell), []);

        // The next edit drops the underline; the messages stay.
        editor().edit.forceActiveFocus(Qt.TabFocusReason);
        keyClick("x");
        compare(current().errorOffset, -1);
        tryVerify(() => !underline.visible);
        compare(marker.opacity, 0);
        compare(current().messages.count, 2);
    }

    // The state follows the current tab.
    function test_theStatusBarFollowsTheCurrentTab() {
        addSqlite(newFile());
        openQuery("SELECT 1; SELECT 2");
        keyClick(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier);
        tryCompare(current(), "state", "done");
        const first = current();
        openQuery("SELECT 3");
        compare(current().state, "idle");
        verify(current() !== first);
        verify(!shell.statusItem.Accessible.name.includes("rows"));
        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        compare(current(), first);
        verify(shell.statusItem.Accessible.name.startsWith("1 row · "), shell.statusItem.Accessible.name);
    }

    // ---- accessibility -----------------------------------------------------------------------

    function test_walkerOnTheConfirmationDialogAndTheResultsRegion() {
        addSqlite(newFile());
        openQuery("DELETE FROM sqlite_master; DELETE FROM sqlite_schema; UPDATE sqlite_master SET name = name");
        keyClick(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(() => shell.confirmRunDialog?.opened);
        const dialog = shell.confirmRunDialog;
        compare(dialog.items.length, 3);
        compare(walker.problems(overlay()), []);
        compare(walker.problems(shell), []);
        compare(dialog.list.Accessible.role, Accessible.List);
        verify(walker.findAll(overlay(), i => i.visible && i.activeFocusOnTab).length >= 3, "list, Cancel and Run anyway");
        const info = a11yProbe.inspect(dialog.cancelButton);
        verify(info.name === "Cancel");
        verify(info.description.includes("3 statements can change or destroy data"), info.description);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        compare(walker.problems(shell.resultsPanel), []);
        compare(walker.problems(shell.resultsPanel.messageList), []);
    }

    // ---- translation -------------------------------------------------------------------------

    function test_spanishStrings() {
        addSqlite(newFile());
        openQuery("DELETE FROM sqlite_master");
        Language.choice = Language.Choice.Spanish;
        compare(CommandRegistry.find("query.cancel").title, "Cancelar consulta");
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryVerify(() => shell.confirmRunDialog?.opened);
        compare(shell.confirmRunDialog.runButton.text, "Ejecutar de todos modos");
        compare(shell.confirmRunDialog.cancelButton.text, "Cancelar");
        compare(shell.confirmRunDialog.title, "¿Ejecutar sentencias peligrosas?");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.confirmRunDialog.visible);

        editor().edit.text = "SELECT 1";
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(current(), "state", "done");
        verify(current().statusText.startsWith("1 fila · "), current().statusText);
        verify(texts().some(t => t.startsWith("Hecho: 1 fila en ")), texts().join("|"));
        compare(shell.resultsPanel.tabBar.currentItem.text, "Resultados");
        compare(walker.problems(shell), []);

        editor().edit.text = "SELECT * FROM nada";
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(current(), "state", "failed");
        compare(current().statusText, "La consulta falló");
        Language.choice = Language.Choice.English;
    }
}
