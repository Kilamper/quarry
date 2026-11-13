import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtTest
import Quarry.Controls

TestCase {
    id: testCase

    name: "Controls"
    when: windowShown
    width: 800
    height: 600
    visible: true

    // ---- helpers -----------------------------------------------------------------------------

    A11y {
        id: walker
    }

    function a11yProblems(root) {
        return walker.problems(root);
    }

    function findAll(root, pred) {
        return walker.findAll(root, pred);
    }

    function isInside(item, ancestor) {
        return walker.isInside(item, ancestor);
    }

    function focusRingOf(control) {
        return findAll(control, i => i.objectName === "focusRing")[0];
    }

    function tab(times) {
        for (let i = 0; i < (times || 1); ++i)
            keyClick(Qt.Key_Tab);
    }

    Component {
        id: sampleComp

        Item {
            id: page

            property alias dialog: dlg
            property alias menu: menu
            property alias openDialog: openBtn
            property alias first: first
            property alias check: check
            property alias combo: combo
            property alias field: field
            property alias iconBtn: iconBtn
            property alias tabs: tabs
            property alias split: split
            property alias leftPane: leftPane
            property int tabCloseCount: 0

            width: 760
            height: 560

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Heading {
                    text: "Sample"
                }

                Icon {
                    text: Icons.search
                }

                RowLayout {
                    QButton {
                        id: first

                        text: "First"
                    }

                    QTextField {
                        id: field

                        accessibleName: "Name"
                    }

                    QCheckBox {
                        id: check

                        text: "Enabled"
                    }

                    QComboBox {
                        id: combo

                        accessibleName: "Engine"
                        model: ["SQLite", "PostgreSQL", "MySQL"]
                    }

                    IconButton {
                        id: iconBtn

                        accessibleName: "Search"
                        shortcutText: "Ctrl+K"
                        iconGlyph: Icons.search
                    }

                    QButton {
                        id: openBtn

                        text: "Open dialog"
                        variant: "primary"
                        onClicked: dlg.open()
                    }

                    QButton {
                        text: "Delete"
                        variant: "danger"
                    }

                    QButton {
                        text: "Flat"
                        variant: "flat"
                    }

                    QButton {
                        text: "Off"
                        enabled: false
                    }
                }

                QTabBar {
                    id: tabs

                    accessibleName: "Documents"

                    QTabButton {
                        text: "One"
                    }

                    QTabButton {
                        text: "Two"
                        closable: true
                        onCloseRequested: page.tabCloseCount++
                    }

                    QTabButton {
                        text: "Three"
                    }
                }

                QSplitView {
                    id: split

                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    accessibleName: "Resize left pane"

                    Item {
                        id: leftPane

                        T.SplitView.preferredWidth: 200
                        T.SplitView.minimumWidth: 40
                    }

                    Item {
                        T.SplitView.fillWidth: true
                    }
                }
            }

            QMenu {
                id: menu

                title: "Actions"

                QMenuItem {
                    text: "Copy"
                    shortcutText: "Ctrl+C"
                }

                QMenuItem {
                    text: "Wrap"
                    checkable: true
                    checked: true
                }
            }

            QDialog {
                id: dlg

                title: "Confirm"

                contentItem: ColumnLayout {
                    QTextField {
                        id: dlgField

                        accessibleName: "Reason"
                    }

                    QButton {
                        id: dlgButton

                        text: "OK"
                        onClicked: dlg.close()
                    }
                }
            }
        }
    }

    // ---- tests -------------------------------------------------------------------------------

    function test_walkerCatchesMissingNames() {
        const bad = createTemporaryObject(badComp, testCase);
        compare(a11yProblems(bad).length, 2); // no name, no role
    }

    Component {
        id: badComp

        Item {
            Item {
                activeFocusOnTab: true
                width: 10
                height: 10
            }
        }
    }

    function test_samplePageHasNoA11yProblems() {
        const page = createTemporaryObject(sampleComp, testCase);
        compare(a11yProblems(page), []);
    }

    function test_mainQmlHasNoA11yProblems() {
        const comp = Qt.createComponent(Qt.resolvedUrl("../../src/app/qml/Main.qml"));
        compare(comp.status, Component.Ready, comp.errorString());
        const win = comp.createObject(null);
        verify(win !== null);
        compare(a11yProblems(win.contentItem), []);
        win.destroy();
    }

    function test_rolesNamesAndStates() {
        const page = createTemporaryObject(sampleComp, testCase);
        compare(page.first.Accessible.role, Accessible.Button);
        compare(page.first.Accessible.name, "First");
        compare(page.iconBtn.Accessible.role, Accessible.Button);
        compare(page.iconBtn.Accessible.name, "Search");
        compare(page.iconBtn.Accessible.description, "Ctrl+K");
        compare(page.field.Accessible.role, Accessible.EditableText);
        compare(page.field.Accessible.name, "Name");
        compare(page.check.Accessible.role, Accessible.CheckBox);
        compare(page.check.Accessible.name, "Enabled");
        compare(page.check.Accessible.checked, false);
        page.check.checked = true;
        compare(page.check.Accessible.checked, true);
        compare(page.combo.Accessible.role, Accessible.ComboBox);
        compare(page.combo.Accessible.name, "Engine");
        compare(page.combo.Accessible.expanded, false);
        compare(page.tabs.Accessible.role, Accessible.PageTabList);
        compare(page.tabs.itemAt(0).Accessible.role, Accessible.PageTab);
        compare(page.tabs.itemAt(0).Accessible.name, "One");
        compare(page.tabs.itemAt(0).Accessible.selected, true);
        compare(page.tabs.itemAt(1).Accessible.selected, false);

        const heading = findAll(page, i => i.Accessible.role === Accessible.Heading);
        compare(heading.length, 1);
        compare(heading[0].Accessible.name, "Sample");
        const icon = findAll(page, i => i.text === Icons.search && i.decorative === true);
        verify(icon.length >= 1);
        verify(icon[0].Accessible.ignored);

        const handles = findAll(page.split, i => i.Accessible.role === Accessible.Splitter);
        compare(handles.length, 1);
        compare(handles[0].Accessible.name, "Resize left pane");
    }

    function test_comboExpandedState() {
        const page = createTemporaryObject(sampleComp, testCase);
        page.combo.popup.open();
        tryVerify(() => page.combo.popup.visible);
        compare(page.combo.Accessible.expanded, true);
        page.combo.popup.close();
        tryVerify(() => !page.combo.popup.visible);
    }

    function test_disabledAndDangerButtons() {
        const page = createTemporaryObject(sampleComp, testCase);
        const buttons = findAll(page, i => i.variant !== undefined && i.text !== undefined);
        const danger = buttons.find(b => b.variant === "danger");
        verify(danger.effectiveGlyph !== ""); // never red alone
        verify(danger.text !== "");
        const off = buttons.find(b => b.text === "Off");
        compare(off.enabled, false);
        verify(off.opacity < 1); // disabled is not color-only
        mouseMove(off, 5, 5);
        compare(off.background.color, Theme.bgCanvas); // no hover fill when disabled
    }

    function test_minimumTargetSizes() {
        const page = createTemporaryObject(sampleComp, testCase);
        for (const c of [page.first, page.iconBtn, page.check, page.combo, page.field, page.tabs.itemAt(0)]) {
            verify(c.implicitHeight >= 24, c + " height " + c.implicitHeight);
            verify(c.implicitWidth >= 24, c + " width " + c.implicitWidth);
        }
        compare(page.iconBtn.implicitWidth >= 24 && page.iconBtn.implicitHeight >= 24, true);
    }

    function test_densityAndZoomMetrics() {
        const page = createTemporaryObject(sampleComp, testCase);
        Theme.density = Theme.Density.Compact;
        Theme.resetZoom();
        compare(page.first.implicitHeight, 24);
        Theme.density = Theme.Density.Comfortable;
        compare(page.first.implicitHeight, 32);
        Theme.zoom = 2.0;
        compare(page.first.implicitHeight, 64);
        Theme.zoom = 0.8;
        verify(page.first.implicitHeight >= 24);
        Theme.density = Theme.Density.Compact;
        Theme.resetZoom();
    }

    function test_tabOrderAndShiftTab() {
        const page = createTemporaryObject(sampleComp, testCase);
        page.first.forceActiveFocus(Qt.TabFocusReason);
        compare(page.first.activeFocus, true);
        const order = [page.field, page.check, page.combo, page.iconBtn, page.openDialog];
        for (const next of order) {
            tab();
            verify(next.activeFocus, "expected focus on " + next + " but " + testCase.Window.activeFocusItem);
            if (next !== page.field) // text fields show the ring whenever they have focus
                verify(next.visualFocus, "keyboard focus must be visible on " + next);
            verify(focusRingOf(next).visible);
        }
        for (const prev of [page.iconBtn, page.combo, page.check, page.field, page.first]) {
            keyClick(Qt.Key_Tab, Qt.ShiftModifier);
            verify(prev.activeFocus, "shift+tab expected " + prev);
        }
    }

    function test_focusRingOnlyForKeyboard() {
        const page = createTemporaryObject(sampleComp, testCase);
        mouseClick(page.first);
        verify(page.first.activeFocus);
        compare(page.first.visualFocus, false);
        compare(focusRingOf(page.first).visible, false);
        tab();
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        verify(page.first.visualFocus);
        verify(focusRingOf(page.first).visible);
    }

    function test_spaceAndEnterActivateButtons() {
        const page = createTemporaryObject(sampleComp, testCase);
        const spy = createTemporaryObject(spyComp, testCase, {
            target: page.first,
            signalName: "clicked"
        });
        page.first.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(spy.count, 1);
        keyClick(Qt.Key_Return);
        compare(spy.count, 2);
        keyClick(Qt.Key_Enter);
        compare(spy.count, 3);
    }

    Component {
        id: spyComp

        SignalSpy {}
    }

    function test_dialogTrapsFocusEscClosesAndRestoresFocus() {
        const page = createTemporaryObject(sampleComp, testCase);
        page.openDialog.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryVerify(() => page.dialog.opened);
        verify(page.dialog.modal);
        // Tab wraps inside the dialog, never reaching the page behind it.
        for (let i = 0; i < 6; ++i) {
            tab();
            const f = testCase.Window.activeFocusItem;
            verify(f !== null, "no active focus item");
            verify(!isInside(f, page), "focus left the dialog: " + f);
        }
        keyClick(Qt.Key_Escape);
        tryVerify(() => !page.dialog.visible);
        tryVerify(() => page.openDialog.activeFocus);
        verify(page.openDialog.visualFocus);
    }

    function test_tabsArrowKeysAndClose() {
        const page = createTemporaryObject(sampleComp, testCase);
        page.tabs.itemAt(0).forceActiveFocus(Qt.TabFocusReason);
        compare(page.tabs.currentIndex, 0);
        keyClick(Qt.Key_Right);
        compare(page.tabs.currentIndex, 1);
        keyClick(Qt.Key_Right);
        compare(page.tabs.currentIndex, 2);
        keyClick(Qt.Key_Left);
        compare(page.tabs.currentIndex, 1);

        // Delete closes the closable tab; its close button has an accessible name.
        keyClick(Qt.Key_Delete);
        compare(page.tabCloseCount, 1);
        const closeBtn = findAll(page.tabs.itemAt(1), i => i.accessibleName !== undefined && i.iconGlyph === Icons.x)[0];
        compare(closeBtn.Accessible.name, "Close Two");
        compare(closeBtn.Accessible.role, Accessible.Button);
        mouseClick(closeBtn);
        compare(page.tabCloseCount, 2);
        // A tab that is not closable ignores Delete.
        page.tabs.itemAt(0).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Delete);
        compare(page.tabCloseCount, 2);
    }

    function test_splitterKeyboardResize() {
        const page = createTemporaryObject(sampleComp, testCase);
        const handle = findAll(page.split, i => i.Accessible.role === Accessible.Splitter)[0];
        verify(handle.activeFocusOnTab);
        handle.forceActiveFocus(Qt.TabFocusReason);
        verify(handle.activeFocus);
        tryCompare(page.leftPane, "width", 200); // wait for the first layout pass
        const before = page.leftPane.width;
        keyClick(Qt.Key_Right);
        tryCompare(page.leftPane, "width", before + Theme.controlHeight);
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Left);
        tryCompare(page.leftPane, "width", before - Theme.controlHeight);
        for (let i = 0; i < 20; ++i)
            keyClick(Qt.Key_Left);
        tryCompare(page.leftPane, "width", 40); // minimumWidth is honored
        verify(focusRingOf(handle).visible);
    }

    function test_iconButtonTooltip() {
        const page = createTemporaryObject(sampleComp, testCase);
        mouseMove(page.iconBtn, 5, 5);
        tryVerify(() => page.iconBtn.tooltip.visible, 3000);
        compare(page.iconBtn.tooltip.text, "Search (Ctrl+K)");
        page.iconBtn.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !page.iconBtn.tooltip.visible);
    }

    function test_reducedMotionZeroesDuration() {
        Theme.animations = Theme.Animations.Off;
        compare(Theme.animationDuration, 0);
        Theme.animations = Theme.Animations.On;
        compare(Theme.animationDuration, 120);
        Theme.animations = Theme.Animations.System;
    }

    function test_menuItems() {
        const page = createTemporaryObject(sampleComp, testCase);
        page.menu.parent = page; // a closed popup needs an item to attach to
        page.menu.open();
        tryVerify(() => page.menu.opened);
        const copy = page.menu.itemAt(0);
        compare(copy.Accessible.role, Accessible.MenuItem);
        compare(copy.Accessible.name, "Copy");
        compare(copy.Accessible.description, "Ctrl+C");
        compare(page.menu.itemAt(1).Accessible.checkable, true);
        compare(page.menu.itemAt(1).Accessible.checked, true);
        verify(copy.implicitHeight >= 24);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !page.menu.visible);
    }
}
