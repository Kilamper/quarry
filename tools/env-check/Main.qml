import QtQuick
import QtQuick.Controls

ApplicationWindow {
    width: 360
    height: 200
    visible: true
    title: qsTr("Environment check")

    Button {
        anchors.centerIn: parent
        text: qsTr("Toolchain OK")
        focus: true
        Accessible.name: qsTr("Toolchain OK")
        Accessible.description: qsTr("Closes the environment check window")
        onClicked: Qt.quit()
    }
}
