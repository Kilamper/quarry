pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// Asks before a plan runs when SqlSafety flagged some of its statements: one dialog lists all of
// them, with the connection and its PRODUCTION mark. The focus starts on Cancel; the destructive
// button is explicit ("Run anyway") and never the default. Esc or Cancel runs nothing.
QDialog {
    id: root

    property string tabId: ""
    property string connectionName: ""
    property bool production: false
    property var items: [] // [{label, risk, sql}]
    // Set when the user chose "Run anyway", so closing does not count as declining.
    property bool answered: false
    readonly property alias cancelButton: cancelBtn
    readonly property alias runButton: runBtn
    readonly property alias list: statements
    readonly property string summary: (items.length === 1 ? qsTr("1 statement can change or destroy data on “%1”.") : qsTr("%1 statements can change or destroy data on “%2”.").arg(items.length)).arg(connectionName) + (production ? " " + qsTr("This is a production connection.") : "")

    signal confirmed(string tabId)
    signal declined(string tabId)

    function openFor(request) {
        tabId = request.tabId;
        connectionName = request.connectionName;
        production = request.production;
        items = request.items;
        open();
    }

    title: qsTr("Run risky statements?")
    width: Math.min(implicitWidth, parent.width - Theme.controlHeight)

    onAboutToShow: answered = false
    onOpened: cancelBtn.forceActiveFocus(Qt.TabFocusReason)
    onClosed: {
        if (!answered)
            declined(tabId);
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing * 3

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: Theme.controlHeight * 18
            text: root.summary
            font.pointSize: Theme.fontSize
            color: Theme.fgDefault
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing * 2

            Text {
                text: qsTr("Connection: %1").arg(root.connectionName)
                font.pointSize: Theme.fontSize
                font.bold: true
                color: Theme.fgDefault
            }

            ProductionBadge {
                visible: root.production
            }
        }

        ListView {
            id: statements

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Theme.controlHeight * 8)
            clip: true
            model: root.items
            spacing: Theme.spacing * 2
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Statements to confirm")
            Accessible.focusable: true

            delegate: ColumnLayout {
                id: entry

                required property var modelData

                width: statements.width
                spacing: 0

                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.label + ": " + modelData.risk + ". " + modelData.sql

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    Icon {
                        text: Icons.triangleAlert
                        color: Theme.danger
                    }

                    Text {
                        Layout.fillWidth: true
                        text: entry.modelData.label + ": " + entry.modelData.risk
                        font.pointSize: Theme.fontSize
                        font.bold: true
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: entry.modelData.sql
                    font.family: Theme.monoFontFamily
                    font.pointSize: Theme.fontSize
                    color: Theme.fgDefault
                    wrapMode: Text.WrapAnywhere
                }
            }

            FocusRing {
                shown: statements.activeFocus
                inset: true
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing * 2

            QButton {
                id: cancelBtn

                text: qsTr("Cancel")
                Accessible.description: root.summary
                onClicked: root.close()
            }

            QButton {
                id: runBtn

                text: qsTr("Run anyway")
                variant: "danger"
                onClicked: {
                    root.answered = true;
                    root.confirmed(root.tabId);
                    root.close();
                }
            }
        }
    }
}
