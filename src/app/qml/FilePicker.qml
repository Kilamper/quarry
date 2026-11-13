import QtQuick
import QtQuick.Dialogs

// The platform file dialog (native where the platform has one, Qt's own otherwise). Loaded on
// demand so QtQuick.Dialogs stays off the startup path.
FileDialog {
    title: qsTr("Choose a database file")
    nameFilters: [qsTr("SQLite databases (*.db *.sqlite *.sqlite3 *.db3)"), qsTr("All files (*)")]
}
