pragma ComponentBehavior: Bound
import QtQuick
import Quarry.Controls

// A landmark of the main window: a named pane with a default focus item. F6 and Mod+1..3 land
// on `defaultFocus` (keyboard reason, so the focus ring shows).
Rectangle {
    id: region

    required property string regionName
    property Item defaultFocus: null

    function focusDefault() {
        if (defaultFocus)
            defaultFocus.forceActiveFocus(Qt.TabFocusReason);
    }

    color: Theme.bgSurface

    Accessible.role: Accessible.Pane
    Accessible.name: regionName
}
