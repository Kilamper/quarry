pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// Every change applies at once (Theme persists its own settings). Esc closes it.
QDialog {
    id: dialog

    // Combo bound to a setting by index. Translating the model resets a combo, so it re-binds.
    component OptionCombo: QComboBox {
        id: combo

        property int value: 0

        signal picked(int index)

        Layout.fillWidth: true
        currentIndex: value
        onModelChanged: currentIndex = Qt.binding(() => combo.value)
        onActivated: index => picked(index)
    }

    title: qsTr("Settings")
    width: Math.min(implicitWidth, parent.width - Theme.controlHeight)

    onOpened: themeCombo.forceActiveFocus(Qt.TabFocusReason)

    contentItem: ColumnLayout {
        spacing: Theme.spacing * 3

        GridLayout {
            columns: 2
            columnSpacing: Theme.spacing * 3
            rowSpacing: Theme.spacing * 2

            Text {
                id: themeLabel

                text: qsTr("Theme")
                font.pointSize: Theme.fontSize
                color: Theme.fgDefault
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            OptionCombo {
                id: themeCombo

                accessibleName: themeLabel.text
                model: [qsTr("System"), qsTr("Light"), qsTr("Dark"), qsTr("High contrast")]
                value: Theme.mode
                onPicked: index => Theme.mode = index
            }

            Text {
                id: densityLabel

                text: qsTr("Density")
                font.pointSize: Theme.fontSize
                color: Theme.fgDefault
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            OptionCombo {
                accessibleName: densityLabel.text
                model: [qsTr("Compact"), qsTr("Comfortable")]
                value: Theme.density
                onPicked: index => Theme.density = index
            }

            Text {
                id: zoomLabel

                text: qsTr("Text size")
                font.pointSize: Theme.fontSize
                color: Theme.fgDefault
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                spacing: Theme.spacing

                IconButton {
                    accessibleName: qsTr("Decrease text size")
                    iconGlyph: Icons.minus
                    onClicked: Theme.zoomOut()
                }

                Text {
                    text: qsTr("%1 %").arg(Math.round(Theme.zoom * 100))
                    font.pointSize: Theme.fontSize
                    color: Theme.fgDefault
                    horizontalAlignment: Text.AlignHCenter
                    Layout.minimumWidth: Theme.controlHeight * 2

                    Accessible.role: Accessible.StaticText
                    Accessible.name: zoomLabel.text + ": " + text
                }

                IconButton {
                    accessibleName: qsTr("Increase text size")
                    iconGlyph: Icons.plus
                    onClicked: Theme.zoomIn()
                }

                QButton {
                    text: qsTr("Reset")
                    onClicked: Theme.resetZoom()
                }
            }

            Text {
                id: animationsLabel

                text: qsTr("Animations")
                font.pointSize: Theme.fontSize
                color: Theme.fgDefault
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            OptionCombo {
                accessibleName: animationsLabel.text
                model: [qsTr("System"), qsTr("On"), qsTr("Off")]
                value: Theme.animations
                onPicked: index => Theme.animations = index
            }

            Text {
                id: languageLabel

                text: qsTr("Language")
                font.pointSize: Theme.fontSize
                color: Theme.fgDefault
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            OptionCombo {
                accessibleName: languageLabel.text
                model: [qsTr("System"), "English", "Español"]
                value: Language.choice
                onPicked: index => Language.choice = index
            }
        }

        QButton {
            text: qsTr("Close")
            variant: "primary"
            Layout.alignment: Qt.AlignRight
            onClicked: dialog.close()
        }
    }
}
