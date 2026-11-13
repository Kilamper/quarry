pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Quarry.Controls

ApplicationWindow {
    id: root

    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    title: qsTr("Quarry")
    color: Theme.bgCanvas
    font.pointSize: Theme.fontSize

    ColumnLayout {
        id: welcome

        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.controlHeight, Theme.controlHeight * 16)
        spacing: Theme.controlHeight / 2

        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Welcome")

        Heading {
            Layout.fillWidth: true
            text: qsTr("Welcome to Quarry")
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Connections and the SQL editor arrive in the next milestone.")
            font: root.font
            color: Theme.fgDefault
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
