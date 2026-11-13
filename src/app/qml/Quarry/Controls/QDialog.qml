pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Modal dialog: Esc closes it and focus goes back to the control that opened it.
T.Dialog {
    id: control

    property var opener: null
    property bool openerKeyboardFocus: false

    parent: T.Overlay.overlay
    anchors.centerIn: T.Overlay.overlay
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, contentWidth + leftPadding + rightPadding, implicitHeaderWidth, implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, contentHeight + topPadding + bottomPadding + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0) + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape
    padding: Theme.spacing * 3
    spacing: Theme.spacing * 2
    font.pointSize: Theme.fontSize

    onAboutToShow: {
        opener = parent ? parent.Window.activeFocusItem : null;
        openerKeyboardFocus = opener !== null && opener.visualFocus === true;
    }
    onClosed: {
        if (opener) {
            const reason = openerKeyboardFocus ? Qt.TabFocusReason : Qt.OtherFocusReason;
            opener.forceActiveFocus(reason);
            // The popup may already have restored focus with its own reason; keep the ring faithful.
            if (opener.focusReason !== undefined)
                opener.focusReason = reason;
        }
        opener = null;
    }

    T.Overlay.modal: Rectangle {
        color: Theme.scrim
    }
    T.Overlay.modeless: Rectangle {
        color: Qt.alpha(Theme.scrim, 0)
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.animationDuration
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.animationDuration
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.bgRaised
        border.width: 1
        border.color: Theme.borderStrong
    }

    header: Heading {
        visible: control.title !== ""
        text: control.title
        level: 2
        wrapMode: Text.NoWrap // a wrapping title makes the dialog's implicit width depend on its width
        topPadding: Theme.spacing * 3
        leftPadding: Theme.spacing * 3
        rightPadding: Theme.spacing * 3
    }
}
