import QtQuick
import QtTest
import Quarry.Controls

// The SQL editor (H1.5): tabs bound to a connection, persistence, the statement band, the run
// commands that only mark and announce, and typing in a 10 000 line file. Driven the way a
// keyboard-only user would be. Temporary database, the real SQLite plugin, no keychain.
TestCase {
    id: testCase

    property var shell: null
    property int fileCounter: 0
    // What QueryController sent to the screen reader.
    property var said: []

    name: "Editor"
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

        function onAnnouncement(text) {
            testCase.said.push(text);
        }
    }

    // ---- helpers -----------------------------------------------------------------------------

    function tabs() {
        return shell.editorTabs;
    }

    function area() {
        return shell.editorArea;
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

    function find(root, pred) {
        return walker.findAll(root, i => i.visible && pred(i))[0];
    }

    function newFile() {
        return testTempDir + "/editor-data-" + (++fileCounter) + ".db";
    }

    function typeText(text) {
        for (const ch of text) {
            if (ch === "\n")
                keyClick(Qt.Key_Return);
            else if (ch === " ")
                keyClick(Qt.Key_Space);
            else
                keyClick(ch);
        }
    }

    // The TreeView and the editor views catch up with a model a pass later (slow CI runners show
    // it): keys sent right after a change would act on the old state.
    function waitForViews() {
        tryVerify(() => shell.sidebar.tree === null ? model().count === 0 : shell.sidebar.tree.rows >= model().count);
        if (tabs().count > 0)
            tryVerify(() => editor() !== null && editor().visible);
    }

    function addProduction(name, file, color) {
        shell.connections.prepare("");
        shell.connections.form.setValue(0, file);
        const spy = createTemporaryObject(spyComp, testCase, {
            target: shell.connections,
            signalName: "connectionSaved"
        });
        verify(shell.connections.save({
            "id": "",
            "providerId": "sqlite",
            "name": name,
            "colorIndex": color,
            "production": name.startsWith("Prod"),
            "passwordMode": 1,
            "password": "",
            "passwordTouched": false
        }));
        tryCompare(spy, "count", 1);
        waitForViews();
        return spy.signalArguments[0][0];
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

    function closeOverlays() {
        for (const d of [shell.closeTabDialog, shell.connectionDialog, shell.deleteDialog, shell.paletteDialog]) {
            if (d && d.visible) {
                tryVerify(() => d.opened || !d.visible);
                d.close();
            }
        }
        tryVerify(() => ![shell.closeTabDialog, shell.connectionDialog, shell.deleteDialog, shell.paletteDialog].some(d => d && d.visible), 5000);
    }

    function removeAll() {
        while (tabs().count > 0)
            tabs().closeTab(0);
        for (let i = model().count - 1; i >= 0; --i)
            shell.connections.remove(model().get(i).connectionId);
        tryCompare(model(), "count", 0);
    }

    function createShell() {
        const comp = Qt.createComponent(Qt.resolvedUrl("../../src/app/qml/MainWindow.qml"));
        compare(comp.status, Component.Ready, comp.errorString());
        const s = comp.createObject(testCase, {
            width: testCase.width,
            height: testCase.height,
            databasePath: testTempDir + "/editor.db",
            providerDirectory: testPluginDir
        });
        verify(s !== null);
        return s;
    }

    function initTestCase() {
        shell = createShell();
        tryVerify(() => !model().loading, 10000);
        tryVerify(() => tabs().loaded, 10000);
    }

    function init() {
        closeOverlays();
        Language.choice = Language.Choice.English;
        Theme.resetZoom();
        shell.resetLayout(); // other suites leave their splitter sizes in the shared settings
        removeAll();
        shell.focusRegion(1);
    }

    function cleanupTestCase() {
        Language.choice = Language.Choice.English;
    }

    // ---- empty state and tabs ----------------------------------------------------------------

    function test_emptyStateOffersNewQuery() {
        compare(tabs().count, 0);
        const empty = area().emptyState;
        verify(empty.visible);
        compare(empty.title, "No query open");
        verify(empty.message.startsWith("Write SQL here."));
        const button = find(empty, i => i.text === "New query" && i.clicked !== undefined);
        verify(button !== undefined);
        compare(walker.problems(shell.regions[1]), []);
        mouseClick(button);
        compare(tabs().count, 1);
        waitForViews();
        verify(editor().edit.activeFocus, "the new query takes the focus");
        verify(!empty.visible);
    }

    function test_modTOpensATabBoundToTheTreeNode() {
        const id = addSqlite(testSampleDb);
        shell.sidebar.selectId(id);
        tryVerify(() => shell.sidebar.currentId === id);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        compare(tabs().count, 1);
        waitForViews();
        compare(tabs().connectionIdOf(0), id);
        compare(tabs().titleOf(0), "Query 1");
        verify(editor().edit.activeFocus);
        compare(area().connectionCombo.displayText, model().get(model().indexOf(id)).name);
        compare(area().connectionCombo.currentIndex, model().indexOf(id));
        verify(area().lastAnnouncement.startsWith("Query 1 opened, connection "), area().lastAnnouncement);
    }

    function test_withoutAConnectionTheTabSaysChooseOne() {
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        compare(tabs().connectionIdOf(0), "");
        compare(area().connectionCombo.displayText, "Choose a connection");
        compare(area().lastAnnouncement, "Query 1 opened, no connection");
        compare(editor().edit.Accessible.description.startsWith("No connection."), true);
    }

    function test_connectionIsChosenFromTheTabHeader() {
        const a = addSqlite(newFile());
        const b = addSqlite(newFile());
        shell.sidebar.selectId(a);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        compare(tabs().connectionIdOf(0), a);
        const combo = area().connectionCombo;
        combo.forceActiveFocus(Qt.TabFocusReason);
        const other = model().indexOf(a) === 0 ? 1 : 0;
        keyClick(model().indexOf(a) === 0 ? Qt.Key_Down : Qt.Key_Up);
        compare(tabs().connectionIdOf(0), model().get(other).connectionId);
        compare(combo.currentIndex, other);
        compare(combo.Accessible.name, "Connection of Query 1");
        compare(combo.Accessible.role, Accessible.ComboBox);
    }

    function test_tabShowsColorAndProductionAsTextAndIcon() {
        const prod = addProduction("Prod shop", newFile(), 3);
        shell.sidebar.selectId(prod);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        const tab = area().tabBar.itemAt(0);
        compare(tab.Accessible.name, "Query 1, Prod shop, production");
        verify(find(tab, i => i.text === "PRODUCTION") !== undefined);
        verify(find(tab, i => i.text === Icons.shieldAlert) !== undefined);
        verify(find(tab, i => i.radius !== undefined && i.width === i.height && i.color.toString() === Theme.connectionColor(3).toString()) !== undefined, "no color dot");
        verify(find(area(), i => i.text === "PRODUCTION" && !tab.contains(tab.mapFromItem(i, 0, 0))) !== undefined, "no PRODUCTION in the header");
        const close = find(tab, i => i.accessibleName === "Close Query 1");
        verify(close !== undefined);
    }

    function test_walkerOnTheEditorRegion() {
        const id = addProduction("Prod shop", newFile(), 1);
        shell.sidebar.selectId(id);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        typeText("select 1");
        compare(walker.problems(shell), []);
        const ed = editor().edit;
        compare(ed.Accessible.role, Accessible.EditableText);
        compare(ed.Accessible.name, "SQL editor, Query 2");
        compare(ed.Accessible.description, "Connection: Prod shop, production. Write SQL here. " + CommandRegistry.displayShortcut("Ctrl+Enter") + " runs the current statement.");
        compare(area().tabBar.Accessible.role, Accessible.PageTabList);
        compare(area().tabBar.itemAt(1).Accessible.role, Accessible.PageTab);
        verify(area().tabBar.itemAt(1).Accessible.selected);

        keyClick(Qt.Key_W, Qt.ControlModifier); // has text: the dialog
        tryVerify(() => shell.closeTabDialog?.opened);
        compare(walker.problems(overlay()), []);
        compare(walker.problems(shell), []);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.closeTabDialog.visible);
    }

    // ---- the keyboard-only flow --------------------------------------------------------------

    function test_keyboardOnlyFlow() {
        shell.focusRegion(1);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        verify(editor().edit.activeFocus);
        typeText("SELECT 1;\nSELECT 2\n  FROM t;\nSELECT 3;");
        compare(tabs().textOf(0), "SELECT 1;\nSELECT 2\n  FROM t;\nSELECT 3;");

        // No connection yet (nothing is selected in the tree): nothing to mark.
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        compare(area().lastAnnouncement, "Choose a connection first.");
        compare(editor().marked.length, 0);

        // The chooser, by keyboard (the connection arrives after the tab; saving selects it in the tree).
        const id = addSqlite(newFile());
        shell.focusRegion(1);
        area().connectionCombo.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down);
        compare(tabs().connectionIdOf(0), id);
        shell.focusRegion(1);
        verify(editor().edit.activeFocus);

        // The cursor is at the end: statement 3 of 3. Up moves into the second statement. From
        // H1.6 on the commands run the statements (QueryController) and announce the start.
        said.length = 0;
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryVerify(() => said.includes("Running statement 3 of 3"));
        tryCompare(shell.queries.current, "state", "done");
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryVerify(() => said.includes("Running statement 2 of 3"));
        compare(editor().marked.length, 1);
        compare(editor().marked[0].startLine, 2);
        compare(editor().marked[0].endLine, 3);
        tryCompare(shell.queries.current, "state", "failed"); // there is no table t
        keyClick(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(() => said.includes("Running statement 1 of 3"));
        compare(editor().marked.length, 3);
        tryCompare(shell.queries.current, "state", "failed"); // all stops at the statement 2 error
        keyClick(Qt.Key_Left); // moving the cursor drops the mark
        compare(editor().marked.length, 0);
        // The keypad's Enter does the same.
        said.length = 0;
        keyClick(Qt.Key_Enter, Qt.ControlModifier);
        tryVerify(() => said.some(t => t.startsWith("Running statement ")), 5000, said.join("|"));

        // Two tabs, Ctrl+Tab and Ctrl+Shift+Tab between them (the focus follows the tab).
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        compare(tabs().currentIndex, 1);
        compare(tabs().connectionIdOf(1), id); // from the active tab
        typeText("SELECT 'two'");
        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        compare(tabs().currentIndex, 0);
        compare(area().lastAnnouncement, "Query 1, tab 1 of 2");
        keyClick(Qt.Key_Tab, Qt.ControlModifier | Qt.ShiftModifier);
        compare(tabs().currentIndex, 1);
        keyClick(Qt.Key_Backtab, Qt.ControlModifier | Qt.ShiftModifier);
        compare(tabs().currentIndex, 0);
        shell.focusRegion(1);
        verify(editor().edit.activeFocus);
        compare(editor().edit.text, "SELECT 1;\nSELECT 2\n  FROM t;\nSELECT 3;"); // each tab kept its own text
        compare(editor().edit.Accessible.name, "SQL editor, Query 1");

        // Mod+W: with text it asks; Esc cancels, nothing is lost.
        keyClick(Qt.Key_W, Qt.ControlModifier);
        tryVerify(() => shell.closeTabDialog?.opened);
        verify(shell.closeTabDialog.cancelButton.activeFocus, "the safe button has the focus");
        verify(shell.closeTabDialog.closeButton.variant === "danger");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.closeTabDialog.visible);
        compare(tabs().count, 2);
        tryVerify(() => editor().edit.activeFocus, 5000, "the focus returns to the editor");

        // Again, and now the explicit button.
        keyClick(Qt.Key_W, Qt.ControlModifier);
        tryVerify(() => shell.closeTabDialog?.opened);
        keyClick(Qt.Key_Tab); // Cancel -> Close and delete text
        verify(shell.closeTabDialog.closeButton.activeFocus);
        keyClick(Qt.Key_Space);
        tryVerify(() => !shell.closeTabDialog.visible);
        compare(tabs().count, 1);
        compare(tabs().titleOf(0), "Query 2");
        compare(area().lastAnnouncement, "Query 1 closed");
        tryVerify(() => editor()?.edit.activeFocus, 5000, "the focus moves to the tab that is left");
        compare(editor().edit.text, "SELECT 'two'");

        // An empty tab closes at once.
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        compare(tabs().count, 2);
        keyClick(Qt.Key_W, Qt.ControlModifier);
        compare(tabs().count, 1);
        verify(!(shell.closeTabDialog?.visible ?? false));
    }

    function test_closeButtonOnTheTabAsksToo() {
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        typeText("x");
        const close = find(area().tabBar.itemAt(0), i => i.accessibleName === "Close Query 1");
        mouseClick(close);
        tryVerify(() => shell.closeTabDialog?.opened);
        compare(shell.closeTabDialog.tabTitle, "Query 1");
        shell.closeTabDialog.closeButton.click();
        tryVerify(() => tabs().count === 0);
        verify(area().emptyState.visible);
        tryVerify(() => area().emptyState.activeFocus, 5000, "with no tab left the empty state takes the focus");
    }

    function test_tabKeyIndentsAndEverythingElseLeaves() {
        keyClick(Qt.Key_T, Qt.ControlModifier);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        keyClick(Qt.Key_Tab);
        typeText("ab");
        compare(editor().edit.text, "\tab");
        verify(editor().edit.activeFocus, "Tab stays in the editor and indents");
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        compare(editor().edit.text, "ab");
        // Ways out: Ctrl+Tab, F6, Mod+1/3.
        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        compare(tabs().currentIndex, 0);
        shell.focusRegion(1);
        keyClick(Qt.Key_F6);
        verify(!editor().edit.activeFocus);
        keyClick(Qt.Key_2, Qt.ControlModifier);
        verify(editor().edit.activeFocus);
        keyClick(Qt.Key_1, Qt.ControlModifier);
        verify(!editor().edit.activeFocus);
        keyClick(Qt.Key_2, Qt.ControlModifier);
        verify(editor().edit.activeFocus);
    }

    function test_arrowKeysOnTheTabBarSwitchTabs() {
        keyClick(Qt.Key_T, Qt.ControlModifier);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        compare(tabs().currentIndex, 1);
        area().tabBar.itemAt(1).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Left);
        compare(tabs().currentIndex, 0);
        tryVerify(() => area().tabBar.itemAt(0).Accessible.selected);
        keyClick(Qt.Key_Right);
        compare(tabs().currentIndex, 1);
        // Delete on a focused tab asks to close it, like the close button.
        keyClick(Qt.Key_Delete);
        tryCompare(tabs(), "count", 1); // empty tab: closed at once
    }

    // ---- line, column, statement band ---------------------------------------------------------

    function test_statusBarSaysLineAndColumn() {
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        compare(shell.cursorSummary, "Ln 1, Col 1");
        typeText("ab\ncde");
        compare(shell.cursorSummary, "Ln 2, Col 4");
        keyClick(Qt.Key_Up);
        compare(shell.cursorSummary, "Ln 1, Col 3");
        const status = find(shell.regions[3], i => i.activeFocusOnTab);
        verify(status.Accessible.name.endsWith("Ln 1, Col 3"), status.Accessible.name);
        const label = find(shell.regions[3], i => i.text === "Ln 1, Col 3");
        verify(label !== undefined);
        keyClick(Qt.Key_T, Qt.ControlModifier); // another tab starts at the top
        compare(shell.cursorSummary, "Ln 1, Col 1");
    }

    function test_theStatementUnderTheCursorIsAlwaysMarked() {
        const id = addSqlite(newFile());
        shell.sidebar.selectId(id);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        typeText("SELECT 1;\nSELECT 2\n  FROM t;\nSELECT 3;");
        // The split follows the text a moment after the last key.
        tryVerify(() => editor().statement.count === 3);
        compare(editor().statement.index, 2);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Up);
        // Cursor moves are immediate: they use the split that is there.
        compare(editor().statement.index, 1);
        compare(editor().statement.startLine, 2);
        compare(editor().statement.endLine, 3);
        keyClick(Qt.Key_Home, Qt.ControlModifier);
        compare(editor().statement.index, 0);
        // The band sits behind the lines of the statement.
        const band = find(editor().flick.contentItem, i => i.color !== undefined && i.color.toString() === Theme.bgHover.toString() && i.height > 0 && i.width > 0);
        verify(band !== undefined, "no band");
        verify(Math.abs(band.height - editor().lineHeight) < 1, "the first statement is one line high");
    }

    // ---- persistence -------------------------------------------------------------------------

    function test_tabsAreRestoredWhenTheShellIsRecreated() {
        const id = addSqlite(newFile());
        shell.sidebar.selectId(id);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        typeText("SELECT 1;\nSELECT 2;");
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        typeText("-- second");
        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        const cursor = tabs().cursorOf(0);
        compare(cursor, 17);

        // Quit within the debounce second: the text must still be there.
        shell.destroy();
        tryVerify(() => CommandRegistry.find("editor.newTab") === null, 10000);
        shell = createShell();
        tryVerify(() => tabs().loaded, 10000);
        compare(tabs().count, 2);
        compare(tabs().titleOf(0), "Query 1");
        compare(tabs().titleOf(1), "Query 2");
        compare(tabs().textOf(0), "SELECT 1;\nSELECT 2;");
        compare(tabs().textOf(1), "-- second");
        compare(tabs().connectionIdOf(0), id);
        compare(tabs().cursorOf(0), cursor);
        compare(tabs().currentIndex, 0);
        tryVerify(() => editor() !== null);
        compare(editor().edit.text, "SELECT 1;\nSELECT 2;");
        compare(editor().edit.cursorPosition, cursor);
        compare(shell.cursorSummary, "Ln 2, Col 8");
        compare(area().tabBar.count, 2);
        tryVerify(() => area().connectionCombo.currentIndex === model().indexOf(id));
        // The second tab's editor is created the first time it is shown.
        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        tryVerify(() => editor()?.edit.text === "-- second");
        compare(walker.problems(shell), []);
    }

    function test_deletingAConnectionKeepsItsTabs() {
        const id = addSqlite(newFile());
        shell.sidebar.selectId(id);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        typeText("select 1");
        shell.connections.remove(id);
        tryCompare(model(), "count", 0);
        compare(tabs().count, 1);
        tryCompare(tabs(), "count", 1);
        tryVerify(() => tabs().connectionIdOf(0) === "");
        compare(area().connectionCombo.displayText, "Choose a connection");
        compare(tabs().textOf(0), "select 1");
    }

    // ---- gutter and large files --------------------------------------------------------------

    function bigText(lines) {
        const out = [];
        for (let i = 1; i <= lines; ++i)
            out.push(i % 7 === 0 ? "-- note " + i : "SELECT id, name FROM t" + i + " WHERE id = " + i + ";");
        return out.join("\n");
    }

    function test_lineNumbersAreVirtualized() {
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        const ed = editor();
        ed.edit.text = bigText(10000);
        tryCompare(ed.edit, "lineCount", 10000);
        const numbers = () => walker.findAll(ed, i => i.lineNumber !== undefined && i.visible);
        // The gutter follows the layout a pass later (a polled wait, to say why when it gives up).
        for (let i = 0; i < 100 && numbers().length <= 5; ++i)
            wait(50);
        verify(numbers().length > 5, "gutter items " + numbers().length + ", editor " + ed.width + "x" + ed.height + ", viewport " + ed.flick.height + ", contentY " + ed.flick.contentY + ", lineHeight " + ed.lineHeight + ", lines " + ed.firstVisibleLine + ".." + ed.lastVisibleLine + " of " + ed.edit.lineCount + ", visible " + ed.visible);
        verify(numbers().length < 120, numbers().length + " number items for 10 000 lines");
        compare(numbers()[0].text, "1");
        // The gutter is as wide as the largest number.
        verify(find(ed, i => i.digitWidth !== undefined).width >= 5 * find(ed, i => i.digitWidth !== undefined).digitWidth);

        // Scrolled to the middle: the numbers follow, aligned with the lines of the text.
        const lh = ed.lineHeight;
        ed.flick.contentY = 5000 * lh;
        tryVerify(() => numbers().some(n => n.text === "5001"));
        const n = numbers().find(x => x.text === "5001");
        const lines = ed.edit.text.split("\n");
        let offset = 0;
        for (let i = 0; i < 5000; ++i)
            offset += lines[i].length + 1;
        const rect = ed.edit.positionToRectangle(offset);
        verify(Math.abs((n.y + ed.flick.contentY) - rect.y) < 1.5, "number y " + (n.y + ed.flick.contentY) + " vs line y " + rect.y);
        verify(numbers().length < 120);

        ed.flick.contentY = ed.flick.contentHeight - ed.flick.height;
        tryVerify(() => numbers().some(x => x.text === "10000"));
        verify(numbers().length < 120);
    }

    function test_pageKeysMoveByAPageNotTheDocument() {
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        const ed = editor();
        ed.edit.text = bigText(500);
        tryCompare(ed.edit, "lineCount", 500);
        keyClick(Qt.Key_Home, Qt.ControlModifier);
        keyClick(Qt.Key_PageDown);
        const line = ed.line;
        verify(line > 3 && line < 100, "PageDown moved to line " + line);
        keyClick(Qt.Key_PageUp);
        compare(ed.line, 1);
    }

    // Types 180 keys (letters, spaces, Return, Backspace) at the start, in the middle and at the end
    // of the editor's text, and returns how long each key took, key press to the text, the
    // highlighting and the model updated.
    function typeAround(ed) {
        const text = ed.edit.text;
        const starts = [0, text.lastIndexOf("\n", text.length / 2) + 1, text.length];
        const keys = ["a", "b", Qt.Key_Return, "c", Qt.Key_Backspace, "d", "e", " ", "f", Qt.Key_Backspace, Qt.Key_Backspace, "g", "h", "i", "j", Qt.Key_Return, Qt.Key_Backspace, "k", "l", "m"];
        const times = [];
        for (const start of starts) {
            ed.edit.cursorPosition = start;
            wait(50); // the view scrolls to the cursor
            for (let rep = 0; rep < 3; ++rep) {
                for (const key of keys) {
                    const t0 = Date.now();
                    keyClick(key);
                    times.push(Date.now() - t0);
                    wait(2); // a typist pauses; the debounce timers get their turn
                }
            }
        }
        return times.sort((a, b) => a - b);
    }

    function percentile(sorted, q) {
        return sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))];
    }

    // The budget: PERFORMANCE.md "typing in the editor (10 000 lines) p95 <= 16 ms per key, with
    // highlighting". What the offscreen platform adds to a key (a software renderer paints the
    // frame) is not the editor's, so the same typing is first timed in a 200 line file, which
    // paints the same viewport: the 10 000 line file may cost at most the budget more than that.
    // tools/bench/bench_editor measures the absolute figure on the real platform. A debug build
    // only has to stay responsive (the limit is relaxed in tst_controls_main.cpp).
    function test_typingInATenThousandLineFileStaysWithinBudget() {
        const id = addSqlite(newFile());
        shell.sidebar.selectId(id);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        const ed = editor();
        // Idle floor of this harness: how late a 5 ms wait returns with nothing going on (the
        // macOS CI runners stall for ~90 ms). A machine like that cannot measure a 16 ms budget.
        let idle = 0;
        for (let i = 0; i < 20; ++i) {
            const t0 = Date.now();
            wait(5);
            idle = Math.max(idle, Date.now() - t0 - 5);
        }
        console.log("IDLE max wait overshoot " + idle + " ms");
        if (idle > testMaxKeystrokeMs / 2)
            skip("idle overshoot " + idle + " ms: too noisy to measure a " + testMaxKeystrokeMs + " ms budget");

        ed.edit.text = bigText(200);
        tryCompare(ed.edit, "lineCount", 200);
        wait(300);
        const small = typeAround(ed);
        ed.edit.text = bigText(10000);
        tryCompare(ed.edit, "lineCount", 10000);
        tryVerify(() => tabs().textOf(0).length === ed.edit.text.length);
        wait(300);
        const big = typeAround(ed);
        console.log("EDITOR10K keystrokes " + big.length + " p50 " + percentile(big, 0.5) + " p95 " + percentile(big, 0.95) + " max " + big[big.length - 1] + " ms (200 lines: p50 " + percentile(small, 0.5) + " p95 " + percentile(small, 0.95) + " ms)");
        verify(percentile(big, 0.95) <= testMaxKeystrokeMs + percentile(small, 0.95), "p95 " + percentile(big, 0.95) + " ms with 10 000 lines, " + percentile(small, 0.95) + " ms with 200: more than " + testMaxKeystrokeMs + " ms apart");
    }

    // ---- translation -------------------------------------------------------------------------

    function test_spanishStrings() {
        Language.choice = Language.Choice.Spanish;
        compare(CommandRegistry.find("editor.newTab").title, "Nueva consulta");
        compare(CommandRegistry.find("editor.closeTab").title, "Cerrar consulta");
        compare(CommandRegistry.find("editor.nextTab").title, "Consulta siguiente");
        compare(CommandRegistry.find("editor.previousTab").title, "Consulta anterior");
        compare(CommandRegistry.find("editor.runStatement").title, "Ejecutar la sentencia actual");
        compare(CommandRegistry.find("editor.runAll").title, "Ejecutar todas las sentencias");
        compare(CommandRegistry.find("editor.runAll").category, "Editor");
        compare(area().emptyState.title, "Ninguna consulta abierta");
        keyClick(Qt.Key_T, Qt.ControlModifier);
        waitForViews();
        compare(tabs().titleOf(0), "Consulta 1");
        compare(area().lastAnnouncement, "Consulta 1 abierta, sin conexión");
        compare(area().connectionCombo.displayText, "Elegir una conexión");
        compare(area().connectionCombo.Accessible.name, "Conexión de Consulta 1");
        compare(editor().edit.Accessible.name, "Editor SQL, Consulta 1");
        compare(shell.cursorSummary, "Lín. 1, Col. 1");
        typeText("select 1");
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        compare(area().lastAnnouncement, "Elige primero una conexión.");
        const id = addSqlite(newFile());
        tabs().setConnection(0, id);
        said.length = 0;
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryVerify(() => said.includes("Ejecutando la sentencia 1 de 1"), 5000, said.join("|"));
        tryVerify(() => said.some(t => t.startsWith("Hecho: 1 fila en ")), 5000, said.join("|"));
        const close = find(area().tabBar.itemAt(0), i => i.accessibleName !== undefined && i.accessibleName.startsWith("Cerrar"));
        compare(close.accessibleName, "Cerrar Consulta 1");
        keyClick(Qt.Key_W, Qt.ControlModifier);
        tryVerify(() => shell.closeTabDialog?.opened);
        compare(shell.closeTabDialog.title, "Cerrar consulta");
        compare(shell.closeTabDialog.closeButton.text, "Cerrar y borrar el texto");
        compare(walker.problems(overlay()), []);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.closeTabDialog.visible);
        Language.choice = Language.Choice.English;
        compare(CommandRegistry.find("editor.newTab").title, "New query");
    }

    // ---- commands ----------------------------------------------------------------------------

    function test_commandsAreRegisteredWithTheirShortcuts() {
        const expected = {
            "editor.newTab": "Ctrl+T",
            "editor.closeTab": "Ctrl+W",
            "editor.nextTab": "Ctrl+Tab",
            "editor.previousTab": "Ctrl+Shift+Tab",
            "editor.runStatement": "Ctrl+Enter",
            "editor.runAll": "Ctrl+Shift+Enter"
        };
        for (const id in expected) {
            const c = CommandRegistry.find(id);
            verify(c !== null, id);
            compare(c.shortcutText, CommandRegistry.displayShortcut(expected[id]), id);
            verify(c.enabled, id);
        }
        compare(CommandRegistry.find("editor.runStatement").title, "Run current statement");
        // With no tab open the run commands say so instead of failing silently.
        CommandRegistry.trigger("editor.runStatement");
        compare(area().lastAnnouncement, "No query is open.");
    }
}
