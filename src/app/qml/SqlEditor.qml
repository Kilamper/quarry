pragma ComponentBehavior: Bound
import QtQuick
import Quarry.Controls

// One SQL editor: a line-number gutter, the text, and two bands behind the text: the statement
// under the cursor (always) and what Mod+Enter / Mod+Shift+Enter would run (until the next edit
// or cursor move). The text and cursor live in EditorTabsModel (`tabs`, row `row`), which saves
// them.
//
// Keyboard: Tab indents and Shift+Tab outdents, so Ctrl+Tab (next tab), F6 and Mod+2..3 are the
// ways out of the editor (WCAG 2.1.2: no keyboard trap).
FocusScope {
    id: root

    required property var tabs // EditorTabsModel
    required property int row
    property string definition: "SQL"
    property string tabTitle: ""
    property string connectionId: ""
    property string connectionName: ""
    property bool production: false
    property string placeholder: ""
    // Ranges marked by a run command: [{startLine, endLine}, ...] (1-based lines).
    property var marked: []

    readonly property alias edit: textEdit
    readonly property alias document: doc
    readonly property alias flick: scroller
    readonly property int line: doc.lineOf(textEdit.cursorPosition)
    readonly property int column: doc.columnOf(textEdit.cursorPosition)
    // Statement under the cursor: {index, count, startLine, endLine, ...}; index -1 = none.
    property var statement: ({
            "index": -1
        })
    // Every line has this height (no wrapping, one font): the gutter relies on it instead of
    // creating an item per line.
    readonly property real lineHeight: textEdit.contentHeight / Math.max(1, textEdit.lineCount)
    readonly property int firstVisibleLine: Math.max(0, Math.floor((scroller.contentY - textEdit.topPadding) / lineHeight))
    readonly property int lastVisibleLine: Math.min(textEdit.lineCount - 1, Math.ceil((scroller.contentY + scroller.height - textEdit.topPadding) / lineHeight))
    property bool restoring: true

    function updateStatement() {
        statement = tabs.statementAt(row, textEdit.cursorPosition);
    }

    function mark(ranges) {
        marked = ranges;
    }

    function ensureCursorVisible() {
        const r = textEdit.cursorRectangle;
        if (r.x < scroller.contentX)
            scroller.contentX = Math.max(0, r.x - Theme.spacing * 4);
        else if (r.x + r.width > scroller.contentX + scroller.width)
            scroller.contentX = r.x + r.width - scroller.width + Theme.spacing * 4;
        if (r.y < scroller.contentY)
            scroller.contentY = r.y;
        else if (r.y + r.height > scroller.contentY + scroller.height)
            scroller.contentY = r.y + r.height - scroller.height;
    }

    // PageUp/PageDown: TextEdit would move by its own height, which inside a Flickable is the
    // whole document.
    function movePage(down, select) {
        const r = textEdit.cursorRectangle;
        const page = Math.max(1, Math.floor(scroller.height / root.lineHeight) - 1) * root.lineHeight;
        const pos = textEdit.positionAt(r.x, r.y + r.height / 2 + (down ? page : -page));
        if (select)
            textEdit.moveCursorSelection(pos, TextEdit.SelectCharacters);
        else
            textEdit.cursorPosition = pos;
    }

    Component.onCompleted: {
        textEdit.text = tabs.textOf(row);
        textEdit.cursorPosition = Math.min(tabs.cursorOf(row), textEdit.length);
        restoring = false;
        updateStatement();
    }

    Connections {
        target: root.tabs

        function onStatementsChanged(changedRow) {
            if (changedRow === root.row)
                root.updateStatement();
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgCanvas
    }

    SqlDocument {
        id: doc

        textDocument: textEdit.textDocument
        definition: root.definition
        theme: Theme
    }

    FontMetrics {
        id: metrics

        font: textEdit.font
    }

    // Line numbers: only the lines in view get an item, so a 10 000 line file costs the same as a
    // short one.
    Item {
        id: gutter

        readonly property real digitWidth: metrics.advanceWidth("0")

        width: Math.max(3, String(textEdit.lineCount).length) * digitWidth + Theme.spacing * 4
        height: parent.height
        clip: true

        Rectangle {
            anchors.fill: parent
            color: Theme.bgSurface
        }

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.borderDefault
        }

        Repeater {
            model: Math.max(0, root.lastVisibleLine - root.firstVisibleLine + 1)

            Text {
                id: number

                required property int index
                readonly property int lineNumber: root.firstVisibleLine + index + 1

                x: gutter.width - width - Theme.spacing * 2
                y: textEdit.topPadding + (lineNumber - 1) * root.lineHeight - scroller.contentY
                height: root.lineHeight
                text: lineNumber
                font.family: textEdit.font.family
                font.pointSize: textEdit.font.pointSize
                verticalAlignment: Text.AlignVCenter
                // The line of the cursor is the strongest, the others are muted (both pass 4.5:1).
                color: lineNumber === root.line ? Theme.fgDefault : Theme.fgMuted
                font.bold: lineNumber === root.line

                Accessible.ignored: true // the screen reader gets the line from the status bar
            }
        }
    }

    Flickable {
        id: scroller

        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: Math.max(width, textEdit.contentWidth + textEdit.leftPadding * 2)
        contentHeight: Math.max(height, textEdit.contentHeight + textEdit.topPadding * 2)
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickDirection

        // The statement under the cursor.
        Rectangle {
            visible: root.statement.index >= 0
            x: 0
            y: textEdit.topPadding + (root.statement.startLine - 1) * root.lineHeight
            width: scroller.contentWidth
            height: (root.statement.endLine - root.statement.startLine + 1) * root.lineHeight
            color: Theme.bgHover
        }

        // What a run command would execute: the same band with an outline, since a fill alone is
        // too faint to tell apart (and is not the only cue: the command announces it).
        Repeater {
            model: root.marked

            Rectangle {
                required property var modelData

                x: 0
                y: textEdit.topPadding + (modelData.startLine - 1) * root.lineHeight
                width: scroller.contentWidth
                height: (modelData.endLine - modelData.startLine + 1) * root.lineHeight
                color: Theme.bgHover
                border.width: Theme.focusRingWidth
                border.color: Theme.accent
            }
        }

        TextEdit {
            id: textEdit

            width: Math.max(scroller.width, contentWidth + leftPadding * 2)
            height: Math.max(scroller.height, contentHeight + topPadding * 2)
            topPadding: Theme.spacing
            leftPadding: Theme.spacing * 2
            font.family: Theme.monoFontFamily
            font.pointSize: Theme.fontSize
            color: Theme.fgDefault
            selectionColor: Theme.selectionBg
            selectedTextColor: Theme.selectionFg
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            textFormat: TextEdit.PlainText
            tabStopDistance: metrics.advanceWidth("0") * 4
            focus: true
            activeFocusOnTab: true
            persistentSelection: true

            Accessible.role: Accessible.EditableText
            Accessible.name: qsTr("SQL editor, %1").arg(root.tabTitle)
            Accessible.description: (root.connectionName !== "" ? qsTr("Connection: %1").arg(root.connectionName) + (root.production ? ", " + qsTr("production") : "") : qsTr("No connection. Choose a connection in the tab header.")) + ". " + root.placeholder
            Accessible.focusable: true
            Accessible.multiLine: true

            onTextChanged: {
                if (root.restoring)
                    return;
                if (root.marked.length > 0)
                    root.marked = [];
                root.tabs.setText(root.row, text);
            }
            onCursorPositionChanged: {
                if (root.restoring)
                    return;
                if (root.marked.length > 0)
                    root.marked = [];
                root.tabs.setCursor(root.row, cursorPosition);
                root.updateStatement();
            }
            onCursorRectangleChanged: root.ensureCursorVisible()

            Keys.onPressed: event => {
                if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                    return;
                const shift = (event.modifiers & Qt.ShiftModifier) !== 0;
                if (event.key === Qt.Key_Tab && !shift) {
                    textEdit.remove(textEdit.selectionStart, textEdit.selectionEnd);
                    textEdit.insert(textEdit.cursorPosition, "\t");
                    event.accepted = true;
                } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && shift)) {
                    doc.outdent(textEdit.cursorPosition);
                    event.accepted = true;
                } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
                    root.movePage(event.key === Qt.Key_PageDown, shift);
                    event.accepted = true;
                }
            }

            Text {
                x: textEdit.leftPadding
                y: textEdit.topPadding
                visible: textEdit.length === 0 && textEdit.preeditText === ""
                text: root.placeholder
                font: textEdit.font
                color: Theme.fgMuted
                Accessible.ignored: true
            }
        }
    }

    FocusRing {
        shown: textEdit.activeFocus
        inset: true
    }
}
