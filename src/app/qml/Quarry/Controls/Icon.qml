pragma ComponentBehavior: Bound
import QtQuick

// Lucide glyph from the Icons singleton: `Icon { text: Icons.search }`. Decorative by default;
// set decorative: false and accessibleName when the icon alone carries meaning.
Text {
    property bool decorative: true
    property string accessibleName: ""

    font.family: Icons.fontFamily
    font.pointSize: Theme.fontSize * 1.1
    color: Theme.fgDefault
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter

    Accessible.ignored: decorative
    Accessible.role: Accessible.Graphic
    Accessible.name: decorative ? "" : accessibleName
}
