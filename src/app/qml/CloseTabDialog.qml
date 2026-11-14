pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// Confirms closing a tab that has text: closing deletes the stored text. The destructive button is
// never the default; the focus starts on Cancel.
QDialog {
    id: root

    property string tabId: ""
    property string tabTitle: ""
    readonly property alias cancelButton: cancelBtn
    readonly property alias closeButton: closeBtn

    signal closeConfirmed(string tabId)

    function openFor(id, title) {
        tabId = id;
        tabTitle = title;
        open();
    }

    title: qsTr("Close query")
    width: Math.min(implicitWidth, parent.width - Theme.controlHeight)

    onOpened: cancelBtn.forceActiveFocus(Qt.TabFocusReason)

    contentItem: ColumnLayout {
        spacing: Theme.spacing * 3

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: Theme.controlHeight * 16
            text: qsTr("Close “%1”? Its text is deleted and cannot be recovered.").arg(root.tabTitle)
            font.pointSize: Theme.fontSize
            color: Theme.fgDefault
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing * 2

            QButton {
                id: cancelBtn

                text: qsTr("Cancel")
                onClicked: root.close()
            }

            QButton {
                id: closeBtn

                text: qsTr("Close and delete text")
                variant: "danger"
                onClicked: {
                    root.closeConfirmed(root.tabId);
                    root.close();
                }
            }
        }
    }
}
