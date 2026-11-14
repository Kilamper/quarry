import QtQuick
import QtTest
import Quarry.Controls

// Saved connections (H1.3): sidebar, connection dialog, delete confirmation, driven the way a
// keyboard-only user would. The database is a temporary one and the SQLite plugin is the real
// one; no test saves a password, so the system keychain is never touched.
TestCase {
    id: testCase

    property var shell: null
    property int fileCounter: 0

    name: "Connections"
    when: windowShown
    width: 900
    height: 700
    visible: true

    A11y {
        id: walker
    }

    // ---- helpers -----------------------------------------------------------------------------

    function overlay() {
        return shell.overlayItem;
    }

    function focused() {
        return testCase.Window.activeFocusItem;
    }

    function model() {
        return shell.connections.model;
    }

    function currentItem() {
        return shell.sidebar.tree.itemAtIndex(shell.sidebar.tree.selectionModel.currentIndex);
    }

    function typeText(text) {
        for (const ch of text)
            keyClick(ch === " " ? Qt.Key_Space : ch);
    }

    function newFile() {
        return testTempDir + "/data-" + (++fileCounter) + ".db";
    }

    function names() {
        const out = [];
        for (let i = 0; i < model().count; ++i)
            out.push(model().get(i).name);
        return out;
    }

    function find(root, pred) {
        return walker.findAll(root, i => i.visible && pred(i))[0];
    }

    function button(text) {
        return find(overlay(), i => i.text === text && i.clicked !== undefined) ?? find(shell, i => i.text === text && i.clicked !== undefined);
    }

    function field(name) {
        return find(overlay(), i => i.accessibleName === name && i.Accessible.role === Accessible.EditableText);
    }

    // Tab until `target` has the focus.
    function tabTo(target) {
        for (let i = 0; i < 60 && !target.activeFocus; ++i)
            keyClick(Qt.Key_Tab);
        verify(target.activeFocus, "Tab never reached " + target);
    }

    function openNew() {
        CommandRegistry.trigger("connection.new");
        tryVerify(() => shell.connectionDialog?.opened);
        tryVerify(() => shell.connectionDialog.nameField.activeFocus);
        return shell.connectionDialog;
    }

    function addSqlite(file) {
        const spy = createTemporaryObject(spyComp, testCase, {
            target: shell.connections,
            signalName: "connectionSaved"
        });
        shell.connections.openSqliteFile(Qt.url("file:///" + file));
        tryCompare(spy, "count", 1);
        return spy.signalArguments[0][0];
    }

    // A production connection with a color, saved the way the dialog does.
    function addProduction(name, file) {
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
            "colorIndex": 3,
            "production": true,
            "passwordMode": 1,
            "password": "",
            "passwordTouched": false
        }));
        tryCompare(spy, "count", 1);
        return spy.signalArguments[0][0];
    }

    function closeOverlays() {
        for (const d of [shell.connectionDialog, shell.deleteDialog, shell.filePicker])
            if (d && d.visible) {
                tryVerify(() => d.opened || !d.visible);
                d.close();
            }
        tryVerify(() => ![shell.connectionDialog, shell.deleteDialog, shell.filePicker].some(d => d && d.visible), 5000);
    }

    function removeAll() {
        for (let i = model().count - 1; i >= 0; --i)
            shell.connections.remove(model().get(i).connectionId);
        tryCompare(model(), "count", 0);
    }

    Component {
        id: spyComp

        SignalSpy {}
    }

    function initTestCase() {
        const comp = Qt.createComponent(Qt.resolvedUrl("../../src/app/qml/MainWindow.qml"));
        compare(comp.status, Component.Ready, comp.errorString());
        shell = comp.createObject(testCase, {
            width: testCase.width,
            height: testCase.height,
            databasePath: testTempDir + "/connections.db",
            providerDirectory: testPluginDir
        });
        verify(shell !== null);
        // Nothing has run yet: the list is still loading, and says so accessibly.
        verify(model().loading);
        const loading = find(shell.sidebar, i => i.Accessible.name === "Loading connections…");
        verify(loading !== undefined, "no accessible loading state");
        tryVerify(() => !model().loading, 10000);
    }

    function init() {
        closeOverlays();
        Language.choice = Language.Choice.English;
        removeAll();
        shell.focusRegion(1);
    }

    // ---- empty and populated sidebar ---------------------------------------------------------

    function test_emptyStateAndWalker() {
        compare(model().count, 0);
        const empty = find(shell.sidebar, i => i.title === "No connections yet");
        verify(empty !== undefined);
        verify(button("New connection") !== undefined);
        verify(button("Open SQLite file") !== undefined);
        compare(walker.problems(shell.sidebar), []);
        compare(shell.sidebar.defaultFocus, shell.sidebar.newButton);
        for (const id of ["connection.edit", "connection.duplicate", "connection.delete"])
            compare(CommandRegistry.find(id).enabled, false, id);
    }

    function test_populatedSidebarIsLabelledAndSelectable() {
        addSqlite(newFile());
        const prod = addProduction("Shop", newFile());
        tryCompare(model(), "count", 2);
        compare(walker.problems(shell.sidebar), []);
        shell.sidebar.selectId(prod);
        tryVerify(() => shell.sidebar.hasCurrent);
        tryVerify(() => currentItem() !== null);
        const item = currentItem();
        compare(item.Accessible.role, Accessible.TreeItem);
        // name, engine, color name and "production": never color or the badge alone
        compare(item.Accessible.name, "Shop, SQLite, Purple, production");
        verify(item.Accessible.selected);
        verify(find(item, i => i.text === "PRODUCTION") !== undefined);
        compare(shell.sidebar.tree.Accessible.role, Accessible.Tree);
        // level and position are said in words (Qt has no level/position-in-set properties)
        compare(item.Accessible.description, "Not connected, " + model().get(model().indexOf(prod)).summary + ", level 1, 2 of 2");
        compare(item.Accessible.expandable, true);
        compare(item.Accessible.expanded, false);

        // The status bar names the selection and marks production with text.
        const status = shell.regions[3];
        verify(find(status, i => i.text === "Selected: Shop") !== undefined);
        verify(find(status, i => i.text === "PRODUCTION") !== undefined);
        verify(status.defaultFocus.Accessible.name.includes("Not connected"));
        verify(status.defaultFocus.Accessible.name.includes("Shop"));
        verify(status.defaultFocus.Accessible.name.includes("production"));
        for (const id of ["connection.edit", "connection.duplicate", "connection.delete"])
            compare(CommandRegistry.find(id).enabled, true, id);
    }

    function test_arrowKeysMoveTheSelection() {
        addSqlite(newFile());
        addSqlite(newFile());
        tryCompare(model(), "count", 2);
        shell.sidebar.tree.goTo(0);
        shell.focusRegion(0);
        verify(shell.sidebar.tree.activeFocus);
        compare(shell.sidebar.tree.cursorRow(), 0);
        keyClick(Qt.Key_Down);
        compare(shell.sidebar.tree.cursorRow(), 1);
        keyClick(Qt.Key_Up);
        compare(shell.sidebar.tree.cursorRow(), 0);
        keyClick(Qt.Key_End);
        compare(shell.sidebar.tree.cursorRow(), 1);
        keyClick(Qt.Key_Home);
        compare(shell.sidebar.tree.cursorRow(), 0);
    }

    function test_contextMenuByKeyboard() {
        addSqlite(newFile());
        shell.focusRegion(0);
        keyClick(Qt.Key_F10, Qt.ShiftModifier);
        const menu = shell.sidebar.menu;
        tryVerify(() => menu.opened);
        compare(menu.count, 4);
        compare([0, 1, 2, 3].map(i => menu.itemAt(i).text), ["Edit", "Duplicate", "Disconnect", "Delete"]);
        compare(menu.itemAt(2).enabled, false, "nothing to disconnect yet");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !menu.visible);
        tryVerify(() => shell.sidebar.tree.activeFocus);
    }

    // ---- the keyboard-only flow --------------------------------------------------------------

    function test_keyboardOnlyFlow() {
        const path = newFile();
        // New connection (Mod+Shift+N), name, file
        keyClick(Qt.Key_N, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(() => shell.connectionDialog?.opened);
        const dlg = shell.connectionDialog;
        tryVerify(() => dlg.nameField.activeFocus);
        compare(dlg.saveButton.enabled, false);
        compare(dlg.saveButton.Accessible.description, "Enter a name to save.");
        typeText("Flow DB");
        compare(dlg.profileName, "Flow DB");
        compare(dlg.saveButton.Accessible.description, "Fill in the required fields to save.");
        keyClick(Qt.Key_Tab);
        const file = field("Database file (or :memory:)");
        verify(file.activeFocus, "Tab from Name must reach the first field");
        typeText(path);
        compare(dlg.saveButton.enabled, true);

        // Test connection: busy state, then the success message, announced
        tabTo(dlg.testButton);
        keyClick(Qt.Key_Return);
        tryCompare(shell.connections, "testState", "success", 15000);
        verify(shell.connections.testMessage.startsWith("Connected to SQLite"), shell.connections.testMessage);
        tryVerify(() => dlg.lastAnnouncement.startsWith("Connected to SQLite"));
        verify(find(overlay(), i => i.text === shell.connections.testMessage) !== undefined);
        // Editing makes the result stale
        file.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_X);
        compare(shell.connections.testState, "idle");
        keyClick(Qt.Key_Backspace);

        // Save
        tabTo(dlg.saveButton);
        keyClick(Qt.Key_Return);
        tryVerify(() => !dlg.visible);
        tryCompare(model(), "count", 1);
        compare(names(), ["Flow DB"]);
        tryCompare(shell.sidebar, "currentName", "Flow DB");
        compare(model().get(0).summary, path.split("/").pop());

        // Edit from the tree's context menu (Enter on a connection now expands it)
        keyClick(Qt.Key_1, Qt.ControlModifier);
        tryVerify(() => shell.sidebar.tree.activeFocus);
        keyClick(Qt.Key_F10, Qt.ShiftModifier);
        tryVerify(() => shell.sidebar.menu.opened);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        tryVerify(() => shell.connectionDialog.opened && shell.connectionDialog.editing);
        compare(shell.connectionDialog.profileName, "Flow DB");
        compare(field("Database file (or :memory:)").text, path);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.connectionDialog.visible);
        tryVerify(() => shell.sidebar.tree.activeFocus, 5000, "focus must return to the tree");

        // Delete with confirmation: Cancel has the focus, Delete is one Tab away
        keyClick(Qt.Key_Delete);
        tryVerify(() => shell.deleteDialog?.opened);
        tryVerify(() => focused().text === "Cancel");
        compare(shell.deleteDialog.production, false);
        keyClick(Qt.Key_Tab);
        compare(focused().text, "Delete");
        keyClick(Qt.Key_Return);
        tryCompare(model(), "count", 0);
        tryVerify(() => !shell.deleteDialog.visible);
        // The list is gone: the focus did not vanish with it
        tryVerify(() => focused() !== null && shell.regionOf(focused()) >= 0);
    }

    function test_editChangesAreSavedAndSorted() {
        addSqlite(newFile());
        const id = addSqlite(newFile());
        tryCompare(model(), "count", 2);
        shell.sidebar.selectId(id);
        shell.openConnectionDialog(id, false);
        tryVerify(() => shell.connectionDialog.opened);
        const dlg = shell.connectionDialog;
        // Engine is fixed when editing
        verify(find(dlg.contentItem, i => i.text === "Engine: SQLite") !== undefined);
        dlg.nameField.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        typeText("Zeta");
        keyClick(Qt.Key_Return); // Enter in a field saves when valid
        tryVerify(() => !dlg.visible);
        tryCompare(model(), "count", 2);
        compare(names()[1], "Zeta");
        compare(shell.sidebar.currentName, "Zeta");
    }

    function test_duplicateCommand() {
        const id = addSqlite(newFile());
        shell.sidebar.selectId(id);
        const original = model().get(0).name;
        CommandRegistry.trigger("connection.duplicate");
        tryCompare(model(), "count", 2);
        verify(names().includes(original + " (copy)"), names().join());
        tryCompare(shell.sidebar, "currentName", original + " (copy)");
        verify(model().get(model().indexOf(shell.sidebar.currentId)).connectionId !== id);
    }

    function test_openSqliteFileHandsTheChosenFileToTheController() {
        // The platform dialog is not driven here (tst_mainwindow runs the command itself); this is
        // what its accepted signal does.
        const file = newFile();
        shell.connections.openSqliteFile(Qt.url("file:///" + file));
        tryCompare(model(), "count", 1);
        compare(model().get(0).providerName, "SQLite");
        compare(model().get(0).name, file.split("/").pop());
    }

    // ---- delete confirmation -----------------------------------------------------------------

    function test_deleteConfirmationWalker() {
        const id = addSqlite(newFile());
        shell.sidebar.selectId(id);
        shell.askDelete(id);
        tryVerify(() => shell.deleteDialog.opened);
        compare(walker.problems(overlay()), []);
        verify(find(overlay(), i => i.text !== undefined && String(i.text).includes("Delete the connection")) !== undefined);
        compare(button("Delete").enabled, true);
    }

    function test_productionDeleteRequiresTypingTheName() {
        const id = addProduction("Prod DB", newFile());
        tryCompare(model(), "count", 1);
        shell.sidebar.selectId(id);
        shell.focusRegion(0);
        keyClick(Qt.Key_Delete);
        tryVerify(() => shell.deleteDialog?.opened);
        const dlg = shell.deleteDialog;
        compare(dlg.production, true);
        compare(walker.problems(overlay()), []);
        verify(find(overlay(), i => i.text === "PRODUCTION") !== undefined);
        const del = button("Delete");
        compare(del.enabled, false);
        compare(del.Accessible.description, "Type the connection name to enable Delete");
        tryVerify(() => focused().accessibleName === "Type the connection name to confirm");
        typeText("Prod D"); // incomplete
        compare(del.enabled, false);
        typeText("b"); // wrong case
        compare(del.enabled, false);
        keyClick(Qt.Key_Backspace);
        typeText("B");
        compare(del.enabled, true);
        // Enter in the name field never deletes
        keyClick(Qt.Key_Return);
        wait(100);
        compare(model().count, 1);
        verify(dlg.visible);
        // Esc cancels
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dlg.visible);
        compare(model().count, 1);

        shell.askDelete(id);
        tryVerify(() => shell.deleteDialog.opened);
        typeText("Prod DB");
        compare(button("Delete").enabled, true);
        mouseClick(button("Delete"));
        tryCompare(model(), "count", 0);
    }

    // ---- connection dialog -------------------------------------------------------------------

    function test_dialogWalkerWithEverythingExpanded() {
        const dlg = openNew();
        compare(walker.problems(overlay()), []);
        const advanced = button("Advanced options");
        verify(advanced !== undefined);
        compare(advanced.Accessible.expandable, true);
        compare(advanced.Accessible.expanded, false);
        verify(field("Busy timeout (ms)") === undefined, "advanced fields start collapsed");
        mouseClick(advanced);
        compare(advanced.Accessible.expanded, true);
        verify(field("Busy timeout (ms)") !== undefined);
        verify(find(overlay(), i => i.text === "Open read-only") !== undefined);
        // A failed test adds the error panel; expand its details too
        dlg.nameField.forceActiveFocus(Qt.TabFocusReason);
        typeText("Walk");
        field("Database file (or :memory:)").forceActiveFocus(Qt.TabFocusReason);
        typeText(testTempDir + "/never-walk.db");
        const ro = find(overlay(), i => i.text === "Open read-only");
        ro.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(ro.checked, true);
        dlg.testButton.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(shell.connections, "testState", "failed", 15000);
        mouseClick(dlg.testError.detailsButton);
        tryVerify(() => dlg.testError.detailsText.visible);
        compare(walker.problems(overlay()), []);
        compare(walker.problems(shell), []);
        // every section is present and named
        for (const name of ["Engine", "Name", "Database file (or :memory:)", "Busy timeout (ms)"])
            verify(find(overlay(), i => i.accessibleName === name) !== undefined, name);
        verify(find(overlay(), i => i.Accessible.role === Accessible.Grouping && i.Accessible.name === "Color") !== undefined);
        verify(button("Test connection") !== undefined);
        compare(button("Save").Accessible.role, Accessible.Button);
        verify(find(overlay(), i => i.text === "Production connection") !== undefined);
    }

    function test_requiredFieldsAreValidatedInline() {
        const dlg = openNew();
        compare(dlg.saveButton.enabled, false);
        // Nothing is shouted at the user before they act
        verify(find(overlay(), i => i.text === "This field is required.") === undefined);
        typeText("Req");
        compare(dlg.saveButton.enabled, false);
        compare(dlg.invalidReason, "Fill in the required fields to save.");

        // Test connection with a missing field marks it, with text and icon, and says so
        dlg.testButton.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const msg = find(overlay(), i => i.text === "This field is required.");
        verify(msg !== undefined);
        compare(shell.connections.testState, "idle");
        tryVerify(() => dlg.lastAnnouncement !== "");
        const file = field("Database file (or :memory:)");
        compare(file.Accessible.description, "Required. This field is required.");

        file.forceActiveFocus(Qt.TabFocusReason);
        typeText(newFile());
        verify(find(overlay(), i => i.text === "This field is required.") === undefined);
        compare(dlg.saveButton.enabled, true);

        // The name is required too
        dlg.nameField.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        keyClick(Qt.Key_Delete);
        keyClick(Qt.Key_Tab); // leaving the field shows the message
        verify(find(overlay(), i => i.text === "Enter a name for the connection.") !== undefined);
        compare(dlg.saveButton.enabled, false);
        compare(dlg.invalidReason, "Enter a name to save.");
    }

    function test_failedTestShowsMessageAndExpandableDetailsAndAnnounces() {
        const dlg = openNew();
        typeText("Broken");
        const file = field("Database file (or :memory:)");
        file.forceActiveFocus(Qt.TabFocusReason);
        const missing = testTempDir + "/missing-connection.db";
        typeText(missing);
        mouseClick(button("Advanced options"));
        const readOnly = find(overlay(), i => i.text === "Open read-only");
        readOnly.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(readOnly.checked, true);

        dlg.testButton.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        // The busy state is an accessible item of its own (it may already be over by now).
        verify(walker.findAll(overlay(), i => i.Accessible.name === "Testing…").length > 0);
        tryCompare(shell.connections, "testState", "failed", 15000);

        const panel = dlg.testError;
        verify(panel.visible);
        compare(panel.title, "Could not connect");
        verify(panel.message !== "");
        verify(panel.detail.includes("missing-connection.db"), panel.detail);
        // plain message first; technical details start collapsed
        verify(!panel.detailsText.visible);
        compare(panel.detailsButton.Accessible.expandable, true);
        compare(panel.detailsButton.Accessible.expanded, false);
        tryVerify(() => dlg.lastAnnouncement.startsWith("Connection failed."));
        verify(dlg.lastAnnouncement.includes(panel.message));

        panel.detailsButton.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        verify(panel.detailsText.visible);
        compare(panel.detailsButton.Accessible.expanded, true);
        // selectable (and so copyable) text, reachable by Tab
        compare(panel.detailsText.readOnly, true);
        compare(panel.detailsText.selectByMouse, true);
        compare(panel.detailsText.Accessible.role, Accessible.EditableText);
        keyClick(Qt.Key_Tab);
        verify(panel.detailsText.activeFocus);
        panel.detailsText.selectAll();
        compare(panel.detailsText.selectedText, panel.detail);

        // Cancelling the dialog drops the result: the next one starts clean
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dlg.visible);
        compare(shell.connections.testState, "idle");
    }

    function test_colorPickerByKeyboard() {
        const dlg = openNew();
        const picker = find(overlay(), i => i.Accessible.role === Accessible.Grouping && i.Accessible.name === "Color");
        verify(picker !== undefined);
        const swatches = walker.findAll(picker, i => i.Accessible.role === Accessible.RadioButton);
        compare(swatches.length, 9);
        compare(swatches.map(s => s.Accessible.name), ["No color", "Blue", "Orange", "Teal", "Purple", "Pink", "Gold", "Green", "Slate"]);
        compare(swatches.filter(s => s.Accessible.checked).length, 1);
        verify(swatches[0].Accessible.checked);
        // a single Tab stop for the whole group
        compare(swatches.filter(s => s.activeFocusOnTab).length, 1);
        for (const s of swatches)
            verify(s.width >= 24 && s.height >= 24);

        swatches[0].forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Right);
        compare(dlg.colorIndex, 0);
        verify(swatches[1].Accessible.checked);
        verify(!swatches[0].Accessible.checked);
        verify(swatches[1].activeFocus);
        keyClick(Qt.Key_Down);
        compare(dlg.colorIndex, 1);
        keyClick(Qt.Key_End);
        compare(dlg.colorIndex, 7);
        keyClick(Qt.Key_Right); // stops at the last color
        compare(dlg.colorIndex, 7);
        keyClick(Qt.Key_Home);
        compare(dlg.colorIndex, -1);
        swatches[4].forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(dlg.colorIndex, 3);
        verify(find(picker, i => i.text === "Purple") !== undefined, "the name is also written out");
        compare(walker.problems(overlay()), []);
    }

    function test_passwordSectionOnlyWhenTheEngineHasASecretField() {
        const dlg = openNew();
        // SQLite has no secret field: no password controls at all
        verify(find(overlay(), i => i.accessibleName === "Password") === undefined);
        verify(find(overlay(), i => i.accessibleName === "Password storage") === undefined);
        compare(shell.connections.form.hasSecret, false);
        dlg.close();
    }

    function test_productionAndColorAreSavedFromTheDialog() {
        const dlg = openNew();
        typeText("Staging");
        field("Database file (or :memory:)").forceActiveFocus(Qt.TabFocusReason);
        typeText(newFile());
        const prod = find(overlay(), i => i.text === "Production connection");
        prod.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(dlg.production, true);
        const picker = find(overlay(), i => i.Accessible.role === Accessible.Grouping && i.Accessible.name === "Color");
        const swatches = walker.findAll(picker, i => i.Accessible.role === Accessible.RadioButton);
        swatches[0].forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Right);
        compare(dlg.colorIndex, 1);
        dlg.saveButton.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(() => !dlg.visible);
        tryCompare(model(), "count", 1);
        compare(model().get(0).production, true);
        compare(model().get(0).colorIndex, 1);
        compare(model().get(0).colorName, "Orange");
        tryCompare(shell.sidebar, "currentProduction", true);
    }

    function test_spanishNamesInTheList() {
        addProduction("Tienda", newFile());
        tryCompare(model(), "count", 1);
        shell.sidebar.selectId(model().get(0).connectionId);
        Language.choice = Language.Choice.Spanish;
        tryVerify(() => currentItem()?.Accessible.name === "Tienda, SQLite, Morado, producción");
        compare(walker.problems(shell.sidebar), []);
        verify(find(shell.sidebar, i => i.text === "PRODUCCIÓN") !== undefined);
        compare(CommandRegistry.find("connection.new").title, "Nueva conexión");
    }
}
