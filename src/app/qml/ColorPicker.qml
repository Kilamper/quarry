pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// "No color" plus the 8 connection colors, as one radio group: a single Tab stop, arrow keys
// move and select. The chosen swatch has a check mark and its name is written out, so the choice
// never depends on seeing the color.
ColumnLayout {
    id: root

    property int colorIndex: -1

    signal colorPicked(int index)

    function nameOf(value) {
        return value < 0 ? qsTr("No color") : Theme.connectionColorName(value);
    }

    function pick(value, focus) {
        root.colorPicked(value);
        if (focus)
            swatches.itemAt(value + 1).forceActiveFocus(Qt.TabFocusReason);
    }

    spacing: Theme.spacing

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Color")

    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Repeater {
            id: swatches

            model: Theme.connectionColorCount + 1

            delegate: Item {
                id: swatch

                required property int index
                readonly property int value: index - 1
                readonly property bool selected: root.colorIndex === value

                width: Theme.controlHeight
                height: Theme.controlHeight
                activeFocusOnTab: selected || activeFocus

                Accessible.role: Accessible.RadioButton
                Accessible.name: root.nameOf(value)
                Accessible.focusable: true
                Accessible.checkable: true
                Accessible.checked: selected
                Accessible.onPressAction: root.pick(value, false)
                Accessible.onToggleAction: root.pick(value, false)

                Keys.onPressed: event => {
                    const last = Theme.connectionColorCount - 1;
                    let next = value;
                    if (event.key === Qt.Key_Right || event.key === Qt.Key_Down)
                        next = Math.min(value + 1, last);
                    else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up)
                        next = Math.max(value - 1, -1);
                    else if (event.key === Qt.Key_Home)
                        next = -1;
                    else if (event.key === Qt.Key_End)
                        next = last;
                    else if (event.key !== Qt.Key_Space && event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
                        return;
                    event.accepted = true;
                    root.pick(next, true);
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacing * 2
                    height: width
                    radius: width / 2
                    color: swatch.value < 0 ? Theme.bgSurface : Theme.connectionColor(swatch.value)
                    border.width: swatch.selected ? Theme.focusRingWidth + 1 : 1
                    border.color: swatch.selected ? Theme.fgDefault : Theme.borderStrong

                    Icon {
                        anchors.centerIn: parent
                        visible: swatch.value < 0
                        text: Icons.x
                        color: Theme.fgMuted
                    }

                    // Contrasting disc behind the check, whatever the swatch color is.
                    Rectangle {
                        anchors.centerIn: parent
                        visible: swatch.selected && swatch.value >= 0
                        width: parent.width * 0.6
                        height: width
                        radius: width / 2
                        color: Theme.bgSurface

                        Icon {
                            anchors.centerIn: parent
                            text: Icons.check
                            color: Theme.fgDefault
                        }
                    }
                }

                TapHandler {
                    onTapped: {
                        root.pick(swatch.value, false);
                        swatch.forceActiveFocus(Qt.MouseFocusReason);
                    }
                }

                FocusRing {
                    shown: swatch.activeFocus
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: root.nameOf(root.colorIndex)
        font.pointSize: Theme.fontSize
        color: Theme.fgMuted
        wrapMode: Text.WordWrap
    }
}
