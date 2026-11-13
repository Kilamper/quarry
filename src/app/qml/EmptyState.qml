pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// Says what the view is, why it is empty and what to do. Children (usually a button) are added
// below the text. With focusable: true the whole block takes Tab focus and is read as one item.
Item {
    id: root

    property string iconGlyph: ""
    property string title: ""
    property string message: ""
    property bool focusable: false
    default property alias content: column.data

    activeFocusOnTab: focusable
    clip: true

    Accessible.role: Accessible.StaticText
    Accessible.name: focusable ? title + ". " + message : ""
    Accessible.ignored: !focusable
    Accessible.focusable: focusable

    ColumnLayout {
        id: column

        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing * 6, Theme.controlHeight * 14)
        spacing: Theme.spacing * 2

        Icon {
            // Dropped when the view is short (large zoom), so the text keeps its room.
            visible: root.iconGlyph !== "" && root.height >= Theme.controlHeight * 6
            text: root.iconGlyph
            font.pointSize: Theme.fontSize * 2
            color: Theme.fgMuted
            Layout.alignment: Qt.AlignHCenter
        }

        Heading {
            Layout.fillWidth: true
            level: 3
            text: root.title
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            text: root.message
            font.pointSize: Theme.fontSize
            color: Theme.fgMuted
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    FocusRing {
        shown: root.focusable && root.activeFocus
        inset: true
    }
}
