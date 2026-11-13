pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// Create or edit a saved connection. The fields come from the engine (ConnectionController.form),
// so the dialog does not know any engine. Save stays disabled, with the reason written next to
// it, until the name and the required fields are filled in.
QDialog {
    id: root

    required property var connections // ConnectionController

    property string editingId: ""
    property string providerId: ""
    property string profileName: ""
    property int colorIndex: -1
    property bool production: false
    property int passwordMode: 1 // 1 = keychain, 2 = ask every time
    property string password: ""
    property bool passwordTouched: false
    property bool advancedOpen: false
    property bool passwordNotCopied: false
    property bool nameTouched: false
    property bool showFieldErrors: false
    property string saveError: ""
    property string saveDetail: ""
    property bool keychainError: false
    // The last thing sent to the screen reader (tests read it; it is not shown).
    property string lastAnnouncement: ""

    readonly property bool editing: editingId !== ""
    readonly property bool nameValid: profileName.trim() !== ""
    readonly property bool valid: nameValid && providerId !== "" && connections.form.valid
    readonly property string invalidReason: providerId === "" ? qsTr("No database engine is available.") : !nameValid ? qsTr("Enter a name to save.") : qsTr("Fill in the required fields to save.")
    readonly property alias flick: flick
    readonly property alias nameField: nameInput
    readonly property alias testButton: testButton
    readonly property alias saveButton: saveButton
    readonly property alias testError: testError

    property int pickRow: -1

    function openFor(id, copied) {
        const d = connections.prepare(id);
        editingId = id;
        providerId = d.providerId ?? "";
        profileName = d.name ?? "";
        colorIndex = d.colorIndex ?? -1;
        production = d.production ?? false;
        passwordMode = d.passwordMode === 2 ? 2 : 1;
        password = "";
        passwordTouched = false;
        passwordNotCopied = copied === true;
        advancedOpen = false;
        nameTouched = false;
        showFieldErrors = false;
        saveError = "";
        saveDetail = "";
        keychainError = false;
        testError.expanded = false;
        open();
    }

    function draft() {
        return {
            "id": editingId,
            "providerId": providerId,
            "name": profileName,
            "colorIndex": colorIndex,
            "production": production,
            "passwordMode": passwordMode,
            "password": password,
            "passwordTouched": passwordTouched
        };
    }

    function announce(text, assertive) {
        lastAnnouncement = text;
        saveButton.Accessible.announce(text, assertive ? Accessible.Assertive : Accessible.Polite);
    }

    function pickFile(row) {
        pickRow = row;
        pickerLoader.active = true;
        pickerLoader.item.open();
    }

    // Any change makes an earlier test result or save error stale.
    function edited() {
        connections.resetTest();
        saveError = "";
    }

    function engineName() {
        for (const e of connections.engines)
            if (e.id === providerId)
                return e.name;
        return providerId;
    }

    function engineRow() {
        for (let i = 0; i < connections.engines.length; ++i)
            if (connections.engines[i].id === providerId)
                return i;
        return -1;
    }

    function trySave() {
        if (!valid) {
            nameTouched = true;
            showFieldErrors = true;
            announce(invalidReason, true);
            return;
        }
        connections.save(draft());
    }

    function runTest() {
        showFieldErrors = true;
        if (!connections.testConnection(draft()))
            announce(qsTr("Fix the fields marked as required before testing."), true);
    }

    // Keeps the focused control inside the scrolled area.
    function reveal(item) {
        if (!item)
            return;
        let inside = false;
        for (let p = item; p; p = p.parent)
            if (p === flick.contentItem) {
                inside = true;
                break;
            }
        if (!inside)
            return;
        const top = item.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + item.height;
        if (top < flick.contentY)
            flick.contentY = Math.max(0, top - Theme.spacing);
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = Math.min(flick.contentHeight - flick.height, bottom - flick.height + Theme.spacing);
    }

    // One generated field of the engine.
    component FieldDelegate: ColumnLayout {
        id: field

        required property int index
        required property string label
        required property string kind
        required property bool isRequired
        required property bool isAdvanced
        required property var enumValues
        required property var value
        required property string error
        property bool advancedSection: false
        property bool touched: false
        readonly property string shownError: touched || root.showFieldErrors ? error : ""

        function setValue(v) {
            root.connections.form.setValue(index, v);
            root.edited();
        }

        Layout.fillWidth: true
        visible: isAdvanced === advancedSection && (!advancedSection || root.advancedOpen)
        spacing: Theme.spacing

        onShownErrorChanged: {
            if (shownError !== "")
                root.announce(label + ": " + shownError, false);
        }

        Text {
            Layout.fillWidth: true
            visible: field.kind !== "bool"
            text: field.isRequired ? qsTr("%1 *").arg(field.label) : field.label
            font.pointSize: Theme.fontSize
            color: Theme.fgDefault
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            visible: field.kind === "text" || field.kind === "number" || field.kind === "path"
            spacing: Theme.spacing

            QTextField {
                id: edit

                Layout.fillWidth: true
                text: String(field.value)
                accessibleName: field.label
                inputMethodHints: field.kind === "number" ? Qt.ImhDigitsOnly : Qt.ImhNone
                onTextEdited: field.setValue(text)
                onEditingFinished: field.touched = true
                onAccepted: root.trySave()

                Accessible.description: [field.isRequired ? qsTr("Required") : "", field.shownError].filter(s => s !== "").join(". ")
            }

            QButton {
                visible: field.kind === "path"
                text: qsTr("Browse…")
                onClicked: root.pickFile(field.index)

                Accessible.name: qsTr("Browse… (%1)").arg(field.label)
            }
        }

        QComboBox {
            Layout.fillWidth: true
            visible: field.kind === "enum"
            accessibleName: field.label
            model: field.enumValues
            currentIndex: field.enumValues.indexOf(field.value)
            onActivated: i => field.setValue(field.enumValues[i])
        }

        QCheckBox {
            Layout.fillWidth: true
            visible: field.kind === "bool"
            text: field.label
            checked: field.value === true
            onToggled: field.setValue(checked)
        }

        RowLayout {
            Layout.fillWidth: true
            visible: field.shownError !== ""
            spacing: Theme.spacing

            Icon {
                text: Icons.circleAlert
                color: Theme.danger
            }

            Text {
                Layout.fillWidth: true
                text: field.shownError
                font.pointSize: Theme.fontSize
                color: Theme.danger
                wrapMode: Text.WordWrap
            }
        }
    }

    title: root.editing ? qsTr("Edit connection") : qsTr("New connection")
    width: Math.min(implicitWidth, parent.width - Theme.controlHeight)

    onOpened: nameInput.forceActiveFocus(Qt.TabFocusReason)
    onClosed: {
        connections.resetTest();
        password = ""; // do not keep the typed password around
    }

    Connections {
        target: root.connections
        enabled: root.visible

        function onConnectionSaved() {
            root.close();
        }

        function onSaveFailed(message, detail, keychain) {
            root.saveError = message;
            root.saveDetail = detail;
            root.keychainError = keychain;
            root.announce(message, true);
        }

        function onTestChanged() {
            const c = root.connections;
            if (c.testState === "testing")
                root.announce(qsTr("Testing…"), false);
            else if (c.testState === "success")
                root.announce(c.testMessage, false);
            else if (c.testState === "failed")
                root.announce(qsTr("Connection failed. %1 %2").arg(c.testMessage).arg(c.testHint), true);
        }
    }

    // Created on first use: the platform file dialog is not needed to show the form.
    Loader {
        id: pickerLoader

        // By URL, not as a type: naming FilePicker would load QtQuick.Dialogs with the dialog.
        active: false
        source: "FilePicker.qml"
    }

    Connections {
        target: pickerLoader.item

        function onAccepted() {
            // qmllint disable missing-property
            root.connections.form.setValue(root.pickRow, root.connections.toLocalPath(pickerLoader.item["selectedFile"]));
            // qmllint enable missing-property
            root.edited();
        }
    }

    Connections {
        target: flick.Window.window

        function onActiveFocusItemChanged() {
            root.reveal(flick.Window.window.activeFocusItem);
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing * 2

        Flickable {
            id: flick

            Layout.fillWidth: true
            Layout.preferredWidth: Theme.controlHeight * 22
            Layout.preferredHeight: Math.min(form.implicitHeight, Math.max(Theme.controlHeight * 6, (root.parent ? root.parent.height : 600) - Theme.controlHeight * 8))
            contentWidth: width
            contentHeight: form.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: form

                width: flick.width
                spacing: Theme.spacing * 2

                // Engine: chosen when creating, fixed afterwards.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    Text {
                        Layout.fillWidth: true
                        visible: !root.editing
                        text: qsTr("Engine")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgDefault
                    }

                    QComboBox {
                        Layout.fillWidth: true
                        visible: !root.editing
                        accessibleName: qsTr("Engine")
                        model: root.connections.engines
                        textRole: "name"
                        currentIndex: root.engineRow()
                        onActivated: i => {
                            root.providerId = root.connections.engines[i].id;
                            root.connections.selectEngine(root.providerId);
                            root.password = "";
                            root.passwordTouched = false;
                            root.edited();
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.editing
                        text: qsTr("Engine: %1").arg(root.engineName())
                        font.pointSize: Theme.fontSize
                        color: Theme.fgMuted
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Name *")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgDefault
                    }

                    QTextField {
                        id: nameInput

                        Layout.fillWidth: true
                        text: root.profileName
                        accessibleName: qsTr("Name")
                        onTextEdited: {
                            root.profileName = text;
                            root.edited();
                        }
                        onEditingFinished: root.nameTouched = true
                        onAccepted: root.trySave()

                        Accessible.description: [qsTr("Required"), nameError.visible ? nameError.text : ""].filter(s => s !== "").join(". ")
                    }

                    RowLayout {
                        id: nameError

                        readonly property string text: qsTr("Enter a name for the connection.")

                        Layout.fillWidth: true
                        visible: !root.nameValid && (root.nameTouched || root.showFieldErrors)
                        spacing: Theme.spacing

                        Icon {
                            text: Icons.circleAlert
                            color: Theme.danger
                        }

                        Text {
                            Layout.fillWidth: true
                            text: nameError.text
                            font.pointSize: Theme.fontSize
                            color: Theme.danger
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Repeater {
                    model: root.connections.form

                    delegate: FieldDelegate {}
                }

                QButton {
                    id: advancedToggle

                    visible: root.connections.form.hasAdvanced
                    variant: "flat"
                    text: qsTr("Advanced options")
                    iconGlyph: root.advancedOpen ? Icons.chevronDown : Icons.chevronRight
                    onClicked: root.advancedOpen = !root.advancedOpen

                    Accessible.expandable: true
                    Accessible.expanded: root.advancedOpen
                }

                Repeater {
                    model: root.connections.form

                    delegate: FieldDelegate {
                        advancedSection: true
                    }
                }

                // Password: only for engines that have a secret field.
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.connections.form.hasSecret
                    spacing: Theme.spacing

                    Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: qsTr("Password")
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.passwordNotCopied
                        text: qsTr("The password was not copied from the original connection. Enter it again.")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgDefault
                        wrapMode: Text.WordWrap
                    }

                    QComboBox {
                        Layout.fillWidth: true
                        accessibleName: qsTr("Password storage")
                        model: [qsTr("Save password in the system keychain"), qsTr("Ask every time")]
                        currentIndex: root.passwordMode === 2 ? 1 : 0
                        onModelChanged: currentIndex = Qt.binding(() => root.passwordMode === 2 ? 1 : 0)
                        onActivated: i => {
                            root.passwordMode = i === 1 ? 2 : 1;
                            root.edited();
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Password")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgDefault
                    }

                    QTextField {
                        Layout.fillWidth: true
                        text: root.password
                        echoMode: TextInput.Password
                        accessibleName: qsTr("Password")
                        placeholderText: root.passwordMode === 2 ? qsTr("Used only to test; never saved") : root.editing && !root.passwordNotCopied ? qsTr("Leave empty to keep the saved password") : ""
                        onTextEdited: {
                            root.password = text;
                            root.passwordTouched = true;
                            root.edited();
                        }
                        onAccepted: root.trySave()
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.passwordMode === 2
                        text: qsTr("The password is asked for every time you connect. It is never saved.")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgMuted
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: qsTr("Color")
                    }

                    ColorPicker {
                        Layout.fillWidth: true
                        colorIndex: root.colorIndex
                        onColorPicked: i => {
                            root.colorIndex = i;
                            root.edited();
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    QCheckBox {
                        id: productionBox

                        Layout.fillWidth: true
                        text: qsTr("Production connection")
                        checked: root.production
                        onToggled: root.production = checked
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Shows a PRODUCTION label next to this connection and asks you to type its name before deleting it.")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgMuted
                        wrapMode: Text.WordWrap
                    }
                }

                // Test connection and its result.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    QButton {
                        id: testButton

                        text: qsTr("Test connection")
                        enabled: root.providerId !== "" && root.connections.testState !== "testing"
                        onClicked: root.runTest()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.connections.testState === "testing"
                        spacing: Theme.spacing

                        Accessible.role: Accessible.StaticText
                        Accessible.name: qsTr("Testing…")

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
                            text: qsTr("Testing…")
                            font.pointSize: Theme.fontSize
                            color: Theme.fgMuted
                        }
                    }

                    RowLayout {
                        id: testOk

                        Layout.fillWidth: true
                        visible: root.connections.testState === "success"
                        spacing: Theme.spacing

                        Accessible.role: Accessible.StaticText
                        Accessible.name: root.connections.testMessage

                        Icon {
                            text: Icons.circleCheck
                            color: Theme.success
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.connections.testMessage
                            font.pointSize: Theme.fontSize
                            color: Theme.fgDefault
                            wrapMode: Text.WordWrap
                        }
                    }

                    ErrorPanel {
                        id: testError

                        Layout.fillWidth: true
                        visible: root.connections.testState === "failed"
                        title: qsTr("Could not connect")
                        message: root.connections.testMessage
                        hint: root.connections.testHint
                        detail: root.connections.testDetail
                    }
                }
            }
        }

        // Save failed: stays visible above the buttons, with the way out for keychain problems.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.saveError !== ""
            spacing: Theme.spacing

            ErrorPanel {
                Layout.fillWidth: true
                title: qsTr("The connection could not be saved")
                message: root.saveError
                detail: root.saveDetail
            }

            QButton {
                visible: root.keychainError
                text: qsTr("Ask every time instead")
                onClicked: {
                    root.passwordMode = 2;
                    root.saveError = "";
                    root.trySave();
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing * 2

            Text {
                Layout.fillWidth: true
                visible: !root.valid
                text: root.invalidReason
                font.pointSize: Theme.fontSize
                color: Theme.fgMuted
                wrapMode: Text.WordWrap
            }

            Item {
                Layout.fillWidth: true
                visible: root.valid
            }

            QButton {
                id: cancelButton

                text: qsTr("Cancel")
                onClicked: root.close()
            }

            QButton {
                id: saveButton

                text: qsTr("Save")
                variant: "primary"
                enabled: root.valid && !root.connections.saving
                onClicked: root.trySave()

                Accessible.description: root.valid ? "" : root.invalidReason
            }
        }
    }
}
