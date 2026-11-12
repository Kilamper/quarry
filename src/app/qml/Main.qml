import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    title: qsTr("Quarry")

    FontMetrics {
        id: fm
    }

    ColumnLayout {
        id: welcome

        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * fm.height, 40 * fm.averageCharacterWidth)
        spacing: fm.height

        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Welcome")

        Label {
            Layout.fillWidth: true
            text: qsTr("Welcome to Quarry")
            font.pointSize: fm.font.pointSize * 1.6
            font.bold: true
            color: root.palette.windowText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap

            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Connections and the SQL editor arrive in the next milestone.")
            color: root.palette.windowText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
