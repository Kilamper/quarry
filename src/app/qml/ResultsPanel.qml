pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// The content of the Results region (UX.md "Ejecucion"): the tabs Results (a summary until the
// grid of H1.7: rows, time, column names) and Messages (what each statement did, notices and
// errors). It shows the state of the current editor tab. An error switches to Messages without
// moving the focus.
ColumnLayout {
    id: root

    required property var queries // QueryController
    readonly property var query: root.queries.current // QueryState
    readonly property alias tabBar: resultsTabs
    readonly property alias messageList: messageList
    readonly property alias summaryItem: summary
    readonly property alias resultsEmpty: resultsEmptyState
    readonly property alias messagesEmpty: messagesEmptyState

    spacing: 0

    Connections {
        target: root.queries

        function onPlanFailed(tabId) {
            if (root.queries.stateOf(tabId) === root.query)
                resultsTabs.currentIndex = 1;
        }
    }

    QTabBar {
        id: resultsTabs

        Layout.fillWidth: true
        accessibleName: qsTr("Results panel tabs")

        QTabButton {
            text: qsTr("Results")
        }

        QTabButton {
            text: qsTr("Messages")
        }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: resultsTabs.currentIndex

        Item {
            EmptyState {
                id: resultsEmptyState

                anchors.fill: parent
                visible: !summary.visible
                iconGlyph: root.query.running ? Icons.loaderCircle : Icons.table
                title: root.query.running ? root.query.statusText : root.query.state === "failed" ? qsTr("The query failed") : root.query.state === "cancelled" ? qsTr("Query cancelled") : qsTr("No results yet")
                message: root.query.running ? qsTr("Results appear here when the query ends.") : root.query.state === "failed" ? qsTr("See the Messages tab for the details.") : qsTr("Run a query to see results here.")
            }

            // Until the grid arrives (H1.7) the result is summarized in words.
            Item {
                id: summary

                anchors.fill: parent
                visible: root.query.hasSummary && !root.query.running
                activeFocusOnTab: visible

                Accessible.role: Accessible.StaticText
                Accessible.name: root.query.statusText + ". " + columnsText.text
                Accessible.focusable: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing * 2
                    spacing: Theme.spacing * 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing

                        Icon {
                            text: Icons.circleCheck
                            color: Theme.success
                        }

                        Heading {
                            Layout.fillWidth: true
                            level: 3
                            text: root.query.statusText
                        }
                    }

                    Text {
                        id: columnsText

                        Layout.fillWidth: true
                        text: root.query.columns.length > 0 ? qsTr("Columns: %1").arg(root.query.columns.join(", ")) : qsTr("This statement returns no columns.")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgDefault
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.query.atEnd
                        text: qsTr("More rows are available. The grid that shows them arrives in a later version.")
                        font.pointSize: Theme.fontSize
                        color: Theme.fgMuted
                        wrapMode: Text.WordWrap
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                FocusRing {
                    shown: summary.activeFocus
                    inset: true
                }
            }
        }

        Item {
            EmptyState {
                id: messagesEmptyState

                anchors.fill: parent
                visible: root.query.messages.count === 0
                iconGlyph: Icons.info
                title: qsTr("No messages.")
                message: qsTr("Errors, notices and affected row counts appear here.")
            }

            ListView {
                id: messageList

                anchors.fill: parent
                anchors.margins: Theme.spacing
                visible: root.query.messages.count > 0
                model: root.query.messages
                clip: true
                spacing: Theme.spacing * 2
                activeFocusOnTab: visible
                boundsBehavior: Flickable.StopAtBounds

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Messages")
                Accessible.focusable: true

                delegate: Item {
                    id: entry

                    required property int index
                    required property string kind
                    required property string type
                    required property string text
                    required property string detail
                    required property int line
                    required property int column

                    width: messageList.width
                    implicitHeight: kind === "error" ? errorPanel.implicitHeight : plain.implicitHeight
                    height: implicitHeight

                    Accessible.role: Accessible.ListItem
                    Accessible.name: type + ": " + text + (line > 0 ? ", " + qsTr("line %1, column %2").arg(line).arg(column) : "")

                    // Errors: icon, the type in words, the message, and the driver text apart.
                    ErrorPanel {
                        id: errorPanel

                        width: parent.width
                        visible: entry.kind === "error"
                        title: entry.type
                        message: entry.text
                        hint: entry.line > 0 ? qsTr("Line %1, column %2").arg(entry.line).arg(entry.column) : ""
                        detail: entry.detail
                    }

                    RowLayout {
                        id: plain

                        width: parent.width
                        visible: entry.kind !== "error"
                        spacing: Theme.spacing

                        Icon {
                            Layout.alignment: Qt.AlignTop
                            text: entry.kind === "statement" ? Icons.circleCheck : Icons.info
                            color: entry.kind === "statement" ? Theme.success : Theme.fgMuted
                        }

                        Text {
                            Layout.fillWidth: true
                            text: entry.kind === "statement" || entry.kind === "cancelled" ? entry.text : entry.type + ": " + entry.text
                            font.pointSize: Theme.fontSize
                            color: Theme.fgDefault
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                FocusRing {
                    shown: messageList.activeFocus
                    inset: true
                }
            }
        }
    }
}
