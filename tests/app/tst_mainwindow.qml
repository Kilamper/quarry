import QtQuick
import QtTest
import Quarry.Controls

// The application frame (src/app/qml/MainWindow.qml): landmarks, F6, panel toggles, command
// palette, settings dialog, translation and 200% zoom. One shell for the whole file, because the
// commands it registers have unique ids.
TestCase {
    id: testCase

    property var shell: null
    property int ran: 0

    name: "MainWindow"
    when: windowShown
    width: 900
    height: 650
    visible: true

    A11y {
        id: walker
    }

    Command {
        id: testCmd

        commandId: "test.run"
        title: "Zzz test action"
        category: "Test"
        onTriggered: testCase.ran++
    }

    Component {
        id: spyComp

        SignalSpy {}
    }

    // ---- helpers -----------------------------------------------------------------------------

    function overlay() {
        return shell.overlayItem;
    }

    function focused() {
        return testCase.Window.activeFocusItem;
    }

    function paletteField() {
        return walker.findAll(overlay(), i => i.accessibleName === "Search commands")[0];
    }

    function paletteList() {
        return walker.findAll(shell.paletteDialog.contentItem, i => i.Accessible.role === Accessible.List)[0];
    }

    function typeText(text) {
        for (const ch of text)
            keyClick(ch === " " ? Qt.Key_Space : ch);
    }

    function openPalette() {
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryVerify(() => shell.paletteDialog?.opened);
        tryVerify(() => paletteField().activeFocus);
    }

    function openSettings() {
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        tryVerify(() => shell.settingsDialog?.opened);
    }

    function overlays() {
        return [shell.paletteDialog, shell.settingsDialog, shell.connectionDialog, shell.deleteDialog, shell.appMenu];
    }

    function closeOverlays() {
        for (const d of overlays())
            if (d && d.visible) {
                tryVerify(() => d.opened || !d.visible); // a popup still entering cannot be closed reliably
                d.close();
            }
        const open = () => overlays().filter(d => d && d.visible).map(d => d.objectName || String(d));
        tryVerify(() => open().length === 0, 5000, "still open: " + open());
    }

    function button(name) {
        const pred = i => i.accessibleName === name && i.clicked !== undefined;
        return walker.findAll(shell, pred).concat(walker.findAll(overlay(), pred))[0];
    }

    function combo(name) {
        return walker.findAll(shell.settingsDialog.contentItem, i => i.accessibleName === name && i.popup !== undefined)[0];
    }

    // A text is clipped when it is elided, wider/taller than the box the layout gave it, or
    // pushed past the right edge of the window. Wrapped texts pass as long as the layout gives
    // them their implicit height; that is what breaks first when a layout is not zoom-safe.
    function textProblems(root) {
        const bad = [];
        const texts = walker.findAll(root, i => i.visible && i.opacity > 0 && i.truncated !== undefined && i.contentWidth !== undefined && i.text !== undefined && i.text !== "");
        for (const t of texts) {
            if (t.truncated)
                bad.push(t.text + ": truncated");
            if (t.contentWidth > t.width + 1)
                bad.push(t.text + ": contentWidth " + t.contentWidth + " > width " + t.width);
            if (t.contentHeight > t.height + 1)
                bad.push(t.text + ": contentHeight " + t.contentHeight + " > height " + t.height);
            if (t.mapToItem(null, 0, 0).x + t.width > testCase.width + 1)
                bad.push(t.text + ": beyond the right edge");
        }
        return {
            count: texts.length,
            problems: bad
        };
    }

    function initTestCase() {
        const comp = Qt.createComponent(Qt.resolvedUrl("../../src/app/qml/MainWindow.qml"));
        compare(comp.status, Component.Ready, comp.errorString());
        shell = comp.createObject(testCase, {
            width: testCase.width,
            height: testCase.height,
            databasePath: testTempDir + "/mainwindow.db",
            providerDirectory: testPluginDir
        });
        verify(shell !== null);
        verify(CommandRegistry.registerCommand(testCmd));
    }

    function cleanupTestCase() {
        Language.choice = Language.Choice.English;
        CommandRegistry.unregisterCommand("test.run");
    }

    function init() {
        closeOverlays();
        Theme.mode = Theme.Mode.System;
        Theme.density = Theme.Density.Compact;
        Theme.animations = Theme.Animations.System;
        Theme.resetZoom();
        Language.choice = Language.Choice.English;
        shell.resetLayout();
        ran = 0;
        shell.focusRegion(1);
        verify(shell.regions[1].defaultFocus.activeFocus);
    }

    // ---- accessibility -----------------------------------------------------------------------

    function test_walkerOnWholeWindow() {
        const focusable = walker.findAll(shell, i => i.visible && i.activeFocusOnTab);
        verify(focusable.length >= 9, "walker saw only " + focusable.length + " focusable items");
        compare(walker.problems(shell), []);
        verify(walker.findAll(shell, i => i.Accessible.role === Accessible.Heading).length >= 4);
    }

    function test_walkerWithPaletteOpen() {
        openPalette();
        compare(walker.problems(overlay()), []);
        compare(walker.problems(shell), []);
        verify(paletteField() !== undefined);
        compare(paletteField().Accessible.role, Accessible.EditableText);
        compare(paletteList().Accessible.role, Accessible.List);
    }

    function test_walkerWithSettingsOpen() {
        openSettings();
        compare(walker.problems(overlay()), []);
        verify(walker.findAll(overlay(), i => i.visible && i.activeFocusOnTab).length >= 6);
    }

    function test_walkerWithConnectionDialogOpen() {
        CommandRegistry.trigger("connection.new");
        tryVerify(() => shell.connectionDialog?.opened);
        compare(walker.problems(overlay()), []);
    }

    function test_regionsAreLandmarks() {
        compare(shell.regions.length, 4);
        for (const r of shell.regions) {
            compare(r.Accessible.role, Accessible.Pane);
            verify(r.Accessible.name !== "");
            verify(r.defaultFocus !== null);
            verify(r.defaultFocus.activeFocusOnTab);
        }
    }

    // ---- regions and keyboard ----------------------------------------------------------------

    function test_f6CyclesRegions() {
        shell.focusRegion(0);
        const order = [1, 2, 3, 0, 1];
        for (const next of order) {
            keyClick(Qt.Key_F6);
            verify(shell.regions[next].defaultFocus.activeFocus, "F6 expected region " + next + " but " + focused());
        }
        const back = [0, 3, 2, 1];
        for (const prev of back) {
            keyClick(Qt.Key_F6, Qt.ShiftModifier);
            verify(shell.regions[prev].defaultFocus.activeFocus, "Shift+F6 expected region " + prev + " but " + focused());
        }
    }

    function test_f6LandsWithVisibleFocusRing() {
        shell.focusRegion(0);
        keyClick(Qt.Key_F6); // editor placeholder
        verify(walker.findAll(shell.regions[1].defaultFocus, i => i.objectName === "focusRing" && i.visible).length === 1);
        keyClick(Qt.Key_F6); // results tab
        verify(shell.regions[2].defaultFocus.visualFocus);
    }

    function test_f6SkipsHiddenRegions() {
        shell.toggleResults();
        shell.focusRegion(1);
        keyClick(Qt.Key_F6);
        verify(shell.regions[3].defaultFocus.activeFocus); // results are hidden, status is next
    }

    function test_modNumberFocusesRegions() {
        keyClick(Qt.Key_1, Qt.ControlModifier);
        verify(shell.regions[0].defaultFocus.activeFocus);
        keyClick(Qt.Key_3, Qt.ControlModifier);
        verify(shell.regions[2].defaultFocus.activeFocus);
        keyClick(Qt.Key_2, Qt.ControlModifier);
        verify(shell.regions[1].defaultFocus.activeFocus);
    }

    function test_tabTraversalHasNoTrap() {
        shell.focusRegion(0);
        const seen = new Set();
        for (let i = 0; i < 40; ++i) {
            keyClick(Qt.Key_Tab);
            seen.add(focused());
        }
        // Every region is reachable by Tab alone, and the walk leaves the sidebar.
        for (const r of shell.regions)
            verify(Array.from(seen).some(f => walker.isInside(f, r)), "Tab never reached " + r.regionName);
        shell.focusRegion(0);
        for (let i = 0; i < 40; ++i)
            keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        verify(focused() !== null);
    }

    // ---- panel toggles -----------------------------------------------------------------------

    function test_toggleResultsMovesFocusAndRestores() {
        shell.focusRegion(2);
        keyClick(Qt.Key_J, Qt.ControlModifier);
        compare(shell.regions[2].visible, false);
        verify(shell.regions[1].defaultFocus.activeFocus, "focus must leave the hidden results panel");
        compare(String(LayoutStore.value("resultsVisible", true)), "false");
        keyClick(Qt.Key_J, Qt.ControlModifier);
        compare(shell.regions[2].visible, true);
        compare(String(LayoutStore.value("resultsVisible", false)), "true");
        keyClick(Qt.Key_3, Qt.ControlModifier);
        verify(shell.regions[2].defaultFocus.activeFocus);
    }

    function test_toggleSidebarMovesFocusAndRestores() {
        shell.focusRegion(0);
        keyClick(Qt.Key_B, Qt.ControlModifier);
        compare(shell.regions[0].visible, false);
        verify(shell.regions[1].defaultFocus.activeFocus);
        keyClick(Qt.Key_B, Qt.ControlModifier);
        compare(shell.regions[0].visible, true);
    }

    function test_focusShortcutRevealsHiddenRegion() {
        shell.toggleSidebar();
        compare(shell.regions[0].visible, false);
        keyClick(Qt.Key_1, Qt.ControlModifier);
        compare(shell.regions[0].visible, true);
        verify(shell.regions[0].defaultFocus.activeFocus);
    }

    function test_toolbarButtonsToggleAndReset() {
        const sidebar = button("Toggle sidebar");
        const results = button("Toggle results panel");
        const reset = button("Reset layout");
        verify(sidebar && results && reset);
        verify(sidebar.height >= 24 && sidebar.width >= 24);
        mouseClick(sidebar);
        compare(shell.regions[0].visible, false);
        mouseClick(results);
        compare(shell.regions[2].visible, false);
        mouseClick(reset);
        compare(shell.regions[0].visible, true);
        compare(shell.regions[2].visible, true);
        compare(sidebar.Accessible.description, CommandRegistry.find("view.toggleSidebar").shortcutText);
    }

    function test_resetLayoutRestoresSizes() {
        const sidebar = shell.regions[0];
        tryVerify(() => sidebar.width > 0);
        const initial = sidebar.width;
        shell.sidebarVisible = false;
        shell.resetLayout();
        compare(shell.sidebarVisible, true);
        tryCompare(sidebar, "width", initial);
        compare(LayoutStore.value("sidebarWidth", "none"), "none");
    }

    function test_splitterResizeIsPersisted() {
        const handle = walker.findAll(shell, i => i.Accessible.role === Accessible.Splitter && i.Accessible.name === "Resize sidebar")[0];
        verify(handle !== undefined);
        handle.forceActiveFocus(Qt.TabFocusReason);
        const before = shell.regions[0].width;
        keyClick(Qt.Key_Right);
        tryCompare(shell.regions[0], "width", before + Theme.controlHeight);
        // Saved a moment after the resize, once the layout has settled.
        tryVerify(() => Number(LayoutStore.value("sidebarWidth", 0)) === Math.round(before + Theme.controlHeight));
    }

    // ---- command palette ---------------------------------------------------------------------

    function test_modKOpensAndEscReturnsFocus() {
        const before = shell.regions[1].defaultFocus;
        openPalette();
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.paletteDialog.visible);
        tryVerify(() => before.activeFocus, 5000, "focus must return to where it was");
    }

    function test_modShiftPOpensToo() {
        keyClick(Qt.Key_P, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(() => shell.paletteDialog?.opened);
    }

    function test_paletteListsEveryCommandWithItsShortcut() {
        openPalette();
        const list = paletteList();
        compare(list.count, CommandRegistry.commands.length);
        verify(list.count >= 15);
        compare(CommandRegistry.find("test.run").title, "Zzz test action");
        typeText("command pal");
        tryCompare(list, "count", 1);
        tryVerify(() => list.currentItem !== null);
        compare(list.currentItem.Accessible.role, Accessible.ListItem);
        compare(list.currentItem.Accessible.name, "Command palette, General, " + CommandRegistry.find("palette.open").shortcutText);
    }

    function test_paletteFiltersAndEmptyState() {
        openPalette();
        typeText("toggle");
        tryCompare(paletteList(), "count", 2);
        for (let i = 0; i < 6; ++i)
            keyClick(Qt.Key_Backspace);
        typeText("qqqq");
        tryCompare(paletteList(), "count", 0);
        const msg = walker.findAll(shell.paletteDialog.contentItem, i => i.text === "No commands match 'qqqq'");
        compare(msg.length, 1);
        verify(msg[0].visible);
        keyClick(Qt.Key_Return); // nothing to run: stays open
        verify(shell.paletteDialog.visible);
    }

    function test_arrowsMoveCurrentItemWhileFocusStaysInField() {
        openPalette();
        typeText("go to");
        const list = paletteList();
        tryCompare(list, "count", 3);
        compare(list.currentIndex, 0);
        keyClick(Qt.Key_Down);
        compare(list.currentIndex, 1);
        verify(paletteField().activeFocus);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(list.currentIndex, 2); // stops at the end
        keyClick(Qt.Key_Up);
        compare(list.currentIndex, 1);
        const id = list.currentItem.commandId;
        const spy = createTemporaryObject(spyComp, testCase, {
            target: CommandRegistry.find(id),
            signalName: "triggered"
        });
        keyClick(Qt.Key_Return);
        tryVerify(() => !shell.paletteDialog.visible);
        tryCompare(spy, "count", 1);
        const regionOfId = {
            "focus.sidebar": 0,
            "focus.editor": 1,
            "focus.results": 2
        };
        tryVerify(() => shell.regions[regionOfId[id]].defaultFocus.activeFocus, 5000, "the command moved focus and the palette did not take it back");
    }

    function test_enterRunsTheCommand() {
        openPalette();
        typeText("zzz");
        tryCompare(paletteList(), "count", 1);
        keyClick(Qt.Key_Enter);
        tryVerify(() => !shell.paletteDialog.visible);
        tryCompare(testCase, "ran", 1);
        // It is now the most recent command: first when the query is empty.
        openPalette();
        compare(paletteList().currentItem.commandId, "test.run");
    }

    function test_disabledCommandIsListedButDoesNotRun() {
        testCmd.enabled = false;
        openPalette();
        typeText("zzz");
        tryCompare(paletteList(), "count", 1);
        keyClick(Qt.Key_Return);
        wait(50);
        verify(shell.paletteDialog.visible);
        compare(ran, 0);
        testCmd.enabled = true;
    }

    function test_everyCommandRunsFromThePalette() {
        for (const c of CommandRegistry.commands.slice()) {
            // Connection commands that act on the selected connection are disabled here: the
            // palette lists them but must not run them (tst_connections.qml covers them enabled).
            if (!c.enabled) {
                verify(c.commandId.startsWith("connection."), c.commandId + " is disabled");
                continue;
            }
            // The platform file dialog cannot be dismissed reliably offscreen: only check that
            // the palette finds it.
            if (c.commandId === "connection.openSqlite") {
                openPalette();
                typeText(c.title.toLowerCase());
                tryVerify(() => paletteList().count >= 1);
                compare(paletteList().currentItem.commandId, c.commandId);
                init();
                continue;
            }
            const spy = createTemporaryObject(spyComp, testCase, {
                target: c,
                signalName: "triggered"
            });
            openPalette();
            typeText(c.title.toLowerCase());
            tryVerify(() => paletteList().count >= 1);
            compare(paletteList().currentItem.commandId, c.commandId);
            keyClick(Qt.Key_Return);
            tryCompare(spy, "count", 1, 2000, c.commandId);
            init();
        }
    }

    function test_menuListsTheSameCommands() {
        mouseClick(button("Menu"));
        tryVerify(() => shell.appMenu?.opened);
        compare(shell.appMenu.count, CommandRegistry.commands.length);
        const toggle = shell.appMenu.itemAt(CommandRegistry.commands.findIndex(c => c.commandId === "view.toggleSidebar"));
        compare(toggle.Accessible.role, Accessible.MenuItem);
        compare(toggle.text, CommandRegistry.find("view.toggleSidebar").title);
        compare(toggle.checked, true);
        compare(toggle.Accessible.description, CommandRegistry.find("view.toggleSidebar").shortcutText); // native text: Cmd on macOS
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.appMenu.visible);
    }

    // ---- settings ----------------------------------------------------------------------------

    function test_settingsOpensAndEscReturnsFocus() {
        const before = shell.regions[1].defaultFocus;
        openSettings();
        tryVerify(() => combo("Theme").activeFocus);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !shell.settingsDialog.visible);
        tryVerify(() => before.activeFocus);
    }

    function test_settingsControlsAreLabelled() {
        openSettings();
        for (const name of ["Theme", "Density", "Animations", "Language"]) {
            const c = combo(name);
            verify(c !== undefined, name);
            compare(c.Accessible.name, name);
            verify(walker.findAll(shell.settingsDialog.contentItem, i => i.text === name && i.Accessible.role !== Accessible.EditableText).length >= 1, "no visible label " + name);
        }
        compare(button("Decrease text size").Accessible.role, Accessible.Button);
        compare(button("Increase text size").Accessible.role, Accessible.Button);
    }

    function test_settingsChangeThemeAndDensityLive() {
        openSettings();
        const theme = combo("Theme");
        theme.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down); // System -> Light
        compare(Theme.mode, Theme.Mode.Light);
        keyClick(Qt.Key_Down);
        compare(Theme.mode, Theme.Mode.Dark);
        verify(Theme.isDark);
        keyClick(Qt.Key_Down);
        compare(Theme.mode, Theme.Mode.HighContrast);
        verify(Theme.isHighContrast);

        const density = combo("Density");
        density.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down);
        compare(Theme.density, Theme.Density.Comfortable);
        compare(Theme.controlHeight, 32);

        const anim = combo("Animations");
        anim.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_End);
        compare(Theme.animations, Theme.Animations.Off);
        compare(Theme.animationDuration, 0);
        // The combo follows the setting when it changes elsewhere.
        Theme.mode = Theme.Mode.Light;
        compare(theme.currentIndex, 1);
    }

    function test_settingsZoomButtons() {
        openSettings();
        mouseClick(button("Increase text size"));
        compare(Theme.zoom, 1.1);
        const pct = walker.findAll(shell.settingsDialog.contentItem, i => i.text === "110 %");
        compare(pct.length, 1);
        compare(pct[0].Accessible.name, "Text size: 110 %");
        mouseClick(button("Decrease text size"));
        mouseClick(button("Decrease text size"));
        compare(Theme.zoom, 0.9);
        mouseClick(walker.findAll(shell.settingsDialog.contentItem, i => i.text === "Reset" && i.clicked !== undefined)[0]);
        compare(Theme.zoom, 1.0);
    }

    function test_zoomShortcuts() {
        keyClick(Qt.Key_Equal, Qt.ControlModifier);
        compare(Theme.zoom, 1.1);
        keyClick(Qt.Key_Minus, Qt.ControlModifier);
        keyClick(Qt.Key_Minus, Qt.ControlModifier);
        compare(Theme.zoom, 0.9);
        keyClick(Qt.Key_0, Qt.ControlModifier);
        compare(Theme.zoom, 1.0);
    }

    // ---- translation -------------------------------------------------------------------------

    function test_languageSwitchRetranslatesAtRuntime() {
        const settings = CommandRegistry.find("settings.open");
        compare(settings.title, "Settings");
        compare(button("Toggle sidebar").Accessible.name, "Toggle sidebar");
        openSettings();
        combo("Language").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_End); // Español
        compare(Language.choice, Language.Choice.Spanish);
        tryCompare(settings, "title", "Ajustes");
        tryCompare(shell.settingsDialog, "title", "Ajustes");
        compare(button("Mostrar u ocultar la barra lateral").Accessible.name, "Mostrar u ocultar la barra lateral");
        compare(shell.regions[0].Accessible.name, "Barra lateral");
        compare(combo("Idioma").currentIndex, 2); // the combo keeps its selection
        compare(walker.problems(shell), []);
        Language.choice = Language.Choice.English;
        tryCompare(settings, "title", "Settings");
    }

    // ---- zoom --------------------------------------------------------------------------------

    // A connection expanded down to its columns: names, details and the indentation of a deep
    // tree must wrap, never be cut off.
    function expandSampleTree() {
        const spy = createTemporaryObject(spyComp, testCase, {
            target: shell.connections,
            signalName: "connectionSaved"
        });
        shell.connections.openSqliteFile(Qt.url("file:///" + testSampleDb));
        tryCompare(spy, "count", 1);
        const tree = shell.sidebar.tree;
        shell.focusRegion(0);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree.rows === 2 && tree.roleAt(1, SchemaTreeModel.NameRole) === "main", 10000);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree.rows === 4);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Right);
        tryVerify(() => tree.rows === 8);
        keyClick(Qt.Key_Right); // customers
        keyClick(Qt.Key_Right); // expand it: Columns, Indexes, Foreign keys
        tryVerify(() => tree.rows === 11);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Right); // expand Columns
        tryVerify(() => tree.rows === 13);
        return spy.signalArguments[0][0];
    }

    function test_layoutSurvives200PercentZoom() {
        const id = expandSampleTree();
        shell.focusRegion(1);
        const base = textProblems(shell);
        verify(base.count >= 20, "only " + base.count + " texts inspected");
        compare(base.problems, []);

        Theme.zoom = 2.0;
        compare(Theme.controlHeight, 48);
        wait(150); // one layout pass
        const zoomed = textProblems(shell);
        verify(zoomed.count >= 10);
        compare(zoomed.problems, []);

        openPalette();
        wait(100);
        const palette = textProblems(overlay());
        verify(palette.count >= 5);
        compare(palette.problems, []);
        closeOverlays();

        openSettings();
        wait(100);
        const settings = textProblems(overlay());
        verify(settings.count >= 8);
        compare(settings.problems, []);
        closeOverlays();

        // The panels still fit: nothing is pushed out of the window vertically.
        for (const r of shell.regions) {
            const p = r.mapToItem(null, 0, 0);
            verify(p.y + r.height <= testCase.height + 1, r.regionName + " leaves the window");
        }

        shell.connections.remove(id);
        tryCompare(shell.connections.model, "count", 0);
    }
}
