pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Split view whose handles are keyboard-operable: Tab to a handle, then arrow keys (Left/Right
// for a horizontal split, Up/Down for a vertical one) move it by one control height.
// The neighbour that is not fillWidth/fillHeight is the one resized.
T.SplitView {
    id: control

    // Name read by screen readers for every handle of this view ("Resize sidebar").
    property string accessibleName: qsTr("Resize panels")

    function resizeAt(handleItem, delta) {
        const horizontal = orientation === Qt.Horizontal;
        const pos = horizontal ? handleItem.x : handleItem.y;
        let prev = -1;
        for (let i = 0; i < count; ++i) {
            const it = itemAt(i);
            if ((horizontal ? it.x + it.width : it.y + it.height) <= pos + 0.5)
                prev = i;
        }
        if (prev < 0 || prev + 1 >= count)
            return;
        const before = itemAt(prev);
        const after = itemAt(prev + 1);
        const beforeFills = horizontal ? before.T.SplitView.fillWidth : before.T.SplitView.fillHeight;
        const target = beforeFills ? after : before;
        const signedDelta = beforeFills ? -delta : delta;
        // Start from the requested size, not the laid-out one: key repeat outruns the layout pass.
        const attached = target.T.SplitView;
        const requested = horizontal ? attached.preferredWidth : attached.preferredHeight;
        const current = horizontal ? target.width : target.height;
        const minimum = horizontal ? attached.minimumWidth : attached.minimumHeight;
        const size = Math.max(minimum, (requested >= 0 ? requested : current) + signedDelta);
        if (horizontal)
            attached.preferredWidth = size;
        else
            attached.preferredHeight = size;
    }

    handle: Rectangle {
        id: handle

        implicitWidth: control.orientation === Qt.Horizontal ? Theme.spacing * 2 : control.width
        implicitHeight: control.orientation === Qt.Horizontal ? control.height : Theme.spacing * 2
        color: T.SplitHandle.pressed || T.SplitHandle.hovered || handle.activeFocus ? Theme.borderStrong : Theme.borderDefault
        activeFocusOnTab: true

        Accessible.role: Accessible.Splitter
        Accessible.name: control.accessibleName
        Accessible.focusable: true

        Keys.onPressed: event => {
            const step = Theme.controlHeight;
            const horizontal = control.orientation === Qt.Horizontal;
            const dec = horizontal ? Qt.Key_Left : Qt.Key_Up;
            const inc = horizontal ? Qt.Key_Right : Qt.Key_Down;
            if (event.key === dec || event.key === inc) {
                control.resizeAt(handle, event.key === inc ? step : -step);
                event.accepted = true;
            }
        }

        FocusRing {
            shown: handle.activeFocus
        }
    }
}
