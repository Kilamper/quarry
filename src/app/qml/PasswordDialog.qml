pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// Asks for the password of an "Ask every time" connection when it is expanded. Enter connects,
// Esc or Cancel leaves the connection closed. The password is never shown or stored on disk.
QDialog {
    id: root

    property string connectionId: ""
    property string connectionName: ""
    property bool retry: false
    // Set when the user chose Connect, so closing does not count as dismissing.
    property bool answered: false
    readonly property alias passwordField: field

    signal submitted(string id, string password)
    signal dismissed(string id)

    function openFor(id, name, isRetry) {
        connectionId = id;
        connectionName = name;
        retry = isRetry;
        open();
    }

    function submit() {
        answered = true;
        submitted(connectionId, field.text);
        close();
    }

    title: qsTr("Password required")
    width: Math.min(implicitWidth, parent.width - Theme.controlHeight)

    onAboutToShow: {
        field.text = "";
        answered = false;
    }
    onOpened: field.forceActiveFocus(Qt.TabFocusReason)
    onClosed: {
        field.text = "";
        if (!answered)
            dismissed(connectionId);
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing * 3

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: Theme.controlHeight * 14
            text: qsTr("Enter the password for “%1”. Quarry keeps it in memory until you close the application.").arg(root.connectionName)
            font.pointSize: Theme.fontSize
            color: Theme.fgDefault
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.retry
            spacing: Theme.spacing

            Icon {
                Layout.alignment: Qt.AlignTop
                text: Icons.circleAlert
                color: Theme.danger
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("The password was not accepted. Try again.")
                font.pointSize: Theme.fontSize
                font.bold: true
                color: Theme.danger
                wrapMode: Text.WordWrap
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Password")
            font.pointSize: Theme.fontSize
            font.bold: true
            color: Theme.fgDefault
            wrapMode: Text.WordWrap
        }

        QTextField {
            id: field

            Layout.fillWidth: true
            echoMode: TextInput.Password
            accessibleName: qsTr("Password")
            // Qt returns no Accessible.name for a password edit; the description carries the label.
            Accessible.description: qsTr("Password")
            Keys.onReturnPressed: root.submit()
            Keys.onEnterPressed: root.submit()
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing * 2

            QButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            QButton {
                text: qsTr("Connect")
                variant: "primary"
                onClicked: root.submit()
            }
        }
    }
}
