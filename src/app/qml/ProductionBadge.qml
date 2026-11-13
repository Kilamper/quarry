pragma ComponentBehavior: Bound
import QtQuick
import Quarry.Controls

// Production marker: icon and text, never color alone. Decorative: the item that shows it already
// names "production" in its accessible name.
Row {
    spacing: Theme.spacing

    Accessible.ignored: true

    Icon {
        anchors.verticalCenter: parent.verticalCenter
        text: Icons.shieldAlert
        color: Theme.danger
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("PRODUCTION")
        font.pointSize: Theme.fontSize
        font.bold: true
        color: Theme.danger
    }
}
