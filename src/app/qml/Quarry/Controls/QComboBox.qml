pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Non-editable combo box. `accessibleName` is its label; the current value is read separately.
T.ComboBox {
    id: control

    property string accessibleName: ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding, Theme.controlHeight)
    verticalPadding: Theme.spacing
    leftPadding: Theme.spacing * 2
    rightPadding: Theme.spacing * 2 + (indicator ? indicator.width + spacing : 0)
    spacing: Theme.spacing
    font.pointSize: Theme.fontSize
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.6

    Accessible.role: Accessible.ComboBox
    Accessible.name: accessibleName !== "" ? accessibleName : displayText
    Accessible.description: displayText
    Accessible.focusable: true
    Accessible.expandable: true
    Accessible.expanded: popup.visible

    delegate: T.ItemDelegate {
        id: item

        required property var model
        required property int index

        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: Math.max(Theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
        verticalPadding: Theme.spacing
        horizontalPadding: Theme.spacing * 2
        font.pointSize: Theme.fontSize
        text: model[control.textRole !== "" ? control.textRole : "modelData"]
        highlighted: control.highlightedIndex === index

        Accessible.role: Accessible.ListItem
        Accessible.name: text
        Accessible.selected: control.currentIndex === index

        contentItem: Text {
            text: item.text
            font: item.font
            color: Theme.fgDefault
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: item.highlighted || item.hovered ? Theme.bgHover : Qt.alpha(Theme.bgRaised, 0)

            // The current value is marked with an accent bar, not only a fill.
            Rectangle {
                visible: control.currentIndex === item.index
                width: Theme.focusRingWidth
                height: parent.height
                color: Theme.accent
            }
        }
    }

    indicator: Icon {
        x: control.width - width - Theme.spacing * 2
        y: control.topPadding + (control.availableHeight - height) / 2
        text: Icons.chevronDown
        color: control.enabled ? Theme.fgDefault : Theme.fgDisabled
    }

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.fgDefault : Theme.fgDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.down ? Theme.bgPressed : control.hovered ? Theme.bgHover : Theme.bgRaised
        border.width: control.visualFocus || control.down || control.hovered ? 2 : 1
        border.color: !control.enabled ? Theme.borderDefault : control.visualFocus ? Theme.focusRing : Theme.borderStrong
    }

    popup: T.Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, Theme.controlHeight * 10)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex

            Accessible.role: Accessible.List
            Accessible.name: control.accessibleName
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.bgRaised
            border.width: 1
            border.color: Theme.borderStrong
        }
    }

    FocusRing {
        shown: control.visualFocus
    }
}
