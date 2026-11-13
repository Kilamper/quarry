pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quarry.Controls

// The sidebar content: saved connections as a list (arrows, Home/End), with the loading, error
// and empty states. Enter edits (until H1.4 adds Connect), Delete deletes after confirming,
// Shift+F10 / Menu / right click open the context menu. Selecting only marks the connection.
Item {
    id: root

    required property var connections // ConnectionController
    property string newShortcut: ""

    readonly property alias list: listView
    readonly property alias newButton: newBtn
    readonly property alias menu: actionMenu
    readonly property var current: listView.currentIndex >= 0 ? listView.currentItem : null
    readonly property bool hasCurrent: current !== null
    readonly property string currentId: current ? current.connectionId : ""
    readonly property string currentName: current ? current.name : ""
    readonly property bool currentProduction: current ? current.production : false
    // First thing F6 / Mod+1 should land on.
    readonly property Item defaultFocus: listView.count > 0 ? listView : newBtn.visible ? newBtn : headerNew

    signal newRequested
    signal openSqliteRequested
    signal editRequested(string id)
    signal duplicateRequested(string id)
    signal deleteRequested(string id)

    function selectId(id) {
        const row = connections.model.indexOf(id);
        if (row >= 0)
            listView.currentIndex = row;
    }

    function popupMenu(item) {
        actionMenu.targetId = item.connectionId;
        actionMenu.popup(item, Theme.spacing * 2, item.height);
    }

    // Keeps the selection on a row when the one it was on disappears.
    Connections {
        target: root.connections.model

        function onCountChanged() {
            if (listView.currentIndex >= listView.count)
                listView.currentIndex = listView.count - 1;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 2
        spacing: Theme.spacing * 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Heading {
                Layout.fillWidth: true
                level: 2
                text: qsTr("Connections")
            }

            IconButton {
                id: headerNew

                accessibleName: qsTr("New connection")
                shortcutText: root.newShortcut
                iconGlyph: Icons.plus
                onClicked: root.newRequested()
            }
        }

        // Load failure, or a failed delete / duplicate / open.
        ColumnLayout {
            id: banner

            readonly property string message: root.connections.errorMessage !== "" ? root.connections.errorMessage : root.connections.model.lastError
            readonly property string detail: root.connections.errorMessage !== "" ? root.connections.errorDetail : root.connections.model.lastErrorDetail

            Layout.fillWidth: true
            visible: message !== ""
            spacing: Theme.spacing

            onMessageChanged: {
                if (message !== "")
                    banner.Accessible.announce(message, Accessible.Assertive);
            }

            // Created when there is something to say: it keeps its text engine off the startup path.
            Loader {
                Layout.fillWidth: true
                active: banner.visible
                sourceComponent: ErrorPanel {
                    title: root.connections.errorMessage !== "" ? qsTr("Something went wrong") : qsTr("Could not load the connections")
                    message: banner.message
                    detail: banner.detail
                }
            }

            RowLayout {
                spacing: Theme.spacing

                QButton {
                    visible: root.connections.errorMessage === ""
                    text: qsTr("Try again")
                    onClicked: root.connections.model.start()
                }

                QButton {
                    visible: root.connections.errorMessage !== ""
                    text: qsTr("Dismiss")
                    onClicked: root.connections.clearError()
                }
            }
        }

        // Loading: shown instead of the empty state so the first frame does not claim "nothing".
        RowLayout {
            Layout.fillWidth: true
            visible: root.connections.model.loading
            spacing: Theme.spacing

            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Loading connections…")

            Icon {
                id: spinner

                text: Icons.loaderCircle
                color: Theme.fgMuted

                RotationAnimator on rotation {
                    running: spinner.visible && Theme.animationDuration > 0
                    from: 0
                    to: 360
                    duration: 1000
                    loops: Animation.Infinite
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Loading connections…")
                font.pointSize: Theme.fontSize
                color: Theme.fgMuted
                wrapMode: Text.WordWrap
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.connections.model.loading && listView.count === 0 && root.connections.model.lastError === ""
            iconGlyph: Icons.database
            title: qsTr("No connections yet")
            message: qsTr("Add a connection to browse its tables and run queries.")

            QButton {
                id: newBtn

                Layout.alignment: Qt.AlignHCenter
                text: qsTr("New connection")
                variant: "primary"
                iconGlyph: Icons.plus
                onClicked: root.newRequested()
            }

            QButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Open SQLite file")
                iconGlyph: Icons.folderOpen
                onClicked: root.openSqliteRequested()
            }
        }

        ListView {
            id: listView

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: count > 0
            clip: true
            model: root.connections.model
            currentIndex: -1
            activeFocusOnTab: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            spacing: 1

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Connections")
            Accessible.focusable: true

            onActiveFocusChanged: {
                if (activeFocus && currentIndex < 0 && count > 0)
                    currentIndex = 0;
            }
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            Keys.onReturnPressed: if (root.hasCurrent) root.editRequested(root.currentId)
            Keys.onEnterPressed: if (root.hasCurrent) root.editRequested(root.currentId)
            Keys.onDeletePressed: if (root.hasCurrent) root.deleteRequested(root.currentId)
            Keys.onMenuPressed: if (root.hasCurrent) root.popupMenu(root.current)
            Keys.onPressed: event => {
                if ((event.key === Qt.Key_Home || event.key === Qt.Key_End) && count > 0) {
                    event.accepted = true;
                    currentIndex = event.key === Qt.Key_Home ? 0 : count - 1;
                } else if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier) && root.hasCurrent) {
                    event.accepted = true;
                    root.popupMenu(root.current);
                }
            }

            delegate: Item {
                id: row

                required property int index
                required property string connectionId
                required property string name
                required property string providerName
                required property string summary
                required property int colorIndex
                required property string colorName
                required property bool production

                readonly property bool isCurrent: ListView.isCurrentItem

                width: ListView.view.width
                height: Math.max(Theme.controlHeight, content.implicitHeight + Theme.spacing * 2)

                Accessible.role: Accessible.ListItem
                Accessible.name: [name, providerName, colorName, production ? qsTr("production") : ""].filter(s => s !== "").join(", ")
                Accessible.description: summary
                Accessible.selected: isCurrent
                Accessible.focusable: true
                Accessible.focused: isCurrent && listView.activeFocus

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius
                    color: row.isCurrent ? Theme.selectionBg : hover.hovered ? Theme.bgHover : Qt.alpha(Theme.bgSurface, 0)

                    // The selected row is marked by a bar as well as by its fill.
                    Rectangle {
                        visible: row.isCurrent
                        width: Theme.focusRingWidth
                        height: parent.height
                        color: Theme.accent
                    }
                }

                RowLayout {
                    id: content

                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing * 2
                    anchors.rightMargin: Theme.spacing * 2
                    spacing: Theme.spacing * 2

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        visible: row.colorIndex >= 0
                        implicitWidth: Theme.spacing * 3
                        implicitHeight: implicitWidth
                        radius: width / 2
                        color: row.colorIndex >= 0 ? Theme.connectionColor(row.colorIndex) : "transparent"
                        border.width: 1
                        border.color: Theme.borderStrong
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            Layout.fillWidth: true
                            text: row.name
                            font.pointSize: Theme.fontSize
                            font.bold: true
                            color: row.isCurrent ? Theme.selectionFg : Theme.fgDefault
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: [row.providerName, row.summary, row.colorName].filter(s => s !== "").join(" · ")
                            font.pointSize: Theme.fontSize
                            color: row.isCurrent ? Theme.selectionFg : Theme.fgMuted
                            wrapMode: Text.WordWrap
                        }

                        ProductionBadge {
                            visible: row.production
                        }
                    }
                }

                HoverHandler {
                    id: hover
                }

                TapHandler {
                    onTapped: {
                        listView.currentIndex = row.index;
                        listView.forceActiveFocus(Qt.MouseFocusReason);
                    }
                    onDoubleTapped: root.editRequested(row.connectionId)
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        listView.currentIndex = row.index;
                        listView.forceActiveFocus(Qt.MouseFocusReason);
                        root.popupMenu(row);
                    }
                }
            }

            FocusRing {
                parent: listView // not the flickable's content item
                shown: listView.activeFocus
                inset: true
            }
        }
    }

    QMenu {
        id: actionMenu

        // The chosen action runs once the menu has closed, so the dialog it opens remembers the
        // list (not the menu) as the place to return the focus to.
        property string targetId: ""
        property string action: ""

        title: qsTr("Connection actions")
        onClosed: {
            const id = targetId;
            const what = action;
            action = "";
            if (what !== "")
                Qt.callLater(() => {
                    if (what === "edit")
                        root.editRequested(id);
                    else if (what === "duplicate")
                        root.duplicateRequested(id);
                    else
                        root.deleteRequested(id);
                });
        }

        QMenuItem {
            text: qsTr("Edit")
            onTriggered: actionMenu.action = "edit"
        }

        QMenuItem {
            text: qsTr("Duplicate")
            onTriggered: actionMenu.action = "duplicate"
        }

        QMenuItem {
            text: qsTr("Delete")
            onTriggered: actionMenu.action = "delete"
        }
    }
}
