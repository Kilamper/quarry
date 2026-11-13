pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// level 1 = page title, 2 = section, 3 = subsection.
T.Label {
    property int level: 1

    font.pointSize: Theme.fontSize * (level === 1 ? 1.6 : level === 2 ? 1.3 : 1.1)
    font.bold: true
    color: Theme.fgDefault
    wrapMode: Text.Wrap // a single long word (zoom, translations) breaks instead of overflowing

    Accessible.role: Accessible.Heading
    Accessible.name: text
}
