pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// Confirms deleting a saved connection. Delete is never the default button; for a production
// connection it stays disabled until the connection name has been typed (UX.md section 1).
QDialog {
    id: root

    property string connectionId: ""
    property string connectionName: ""
    property bool production: false
    readonly property bool confirmed: !production || nameField.text === connectionName

    signal deleteConfirmed(string id)

    function openFor(id, name, isProduction) {
        connectionId = id;
        connectionName = name;
        production = isProduction;
        open();
    }

    title: qsTr("Delete connection")
    width: Math.min(implicitWidth, parent.width - Theme.controlHeight)

    onAboutToShow: nameField.text = ""
    // Production: the field is the next step. Otherwise the safe button gets the focus.
    onOpened: (production ? nameField : cancelButton).forceActiveFocus(Qt.TabFocusReason)

    contentItem: ColumnLayout {
        spacing: Theme.spacing * 3

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: Theme.controlHeight * 16
            text: qsTr("Delete the connection “%1”? Its saved password is removed too. The database itself is not touched.").arg(root.connectionName)
            font.pointSize: Theme.fontSize
            color: Theme.fgDefault
            wrapMode: Text.WordWrap
        }

        ProductionBadge {
            visible: root.production
        }

        Text {
            id: typeHint

            Layout.fillWidth: true
            visible: root.production
            text: qsTr("This is a production connection. Type its name to confirm.")
            font.pointSize: Theme.fontSize
            color: Theme.fgDefault
            wrapMode: Text.WordWrap
        }

        QTextField {
            id: nameField

            Layout.fillWidth: true
            visible: root.production
            accessibleName: qsTr("Type the connection name to confirm")
            placeholderText: root.connectionName
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing * 2

            QButton {
                id: cancelButton

                text: qsTr("Cancel")
                onClicked: root.close()
            }

            QButton {
                id: deleteButton

                text: qsTr("Delete")
                variant: "danger"
                enabled: root.confirmed
                Accessible.description: root.confirmed ? "" : qsTr("Type the connection name to enable Delete")
                onClicked: {
                    root.deleteConfirmed(root.connectionId);
                    root.close();
                }
            }
        }
    }
}
