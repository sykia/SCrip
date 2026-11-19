import QtQuick

Item {
    id: input
    required property var selection
    required property int rowIndex
    required property var host
    property bool mobile: false
    readonly property bool eligible: !!selection && selection.eligible(rowIndex)
    visible: eligible
    focus: false
    Accessible.role: Accessible.CheckBox
    Accessible.name: qsTr("Выбрать «") + (selection?.rows[rowIndex]?.title || qsTr("трек")) + "»"
    Accessible.checked: !!selection && selection.contains(rowIndex)
    Accessible.onToggleAction: selection.choose(rowIndex)
    activeFocusOnTab: true
    Keys.onSpacePressed: event => { selection.choose(rowIndex, event.modifiers); event.accepted = true }
    Keys.onEscapePressed: event => { event.accepted = selection.active; if (selection.active && !selection.busy) selection.reset() }
    Rectangle { anchors.fill: parent; radius: 10; visible: (!!input.selection && input.selection.contains(input.rowIndex)) || input.activeFocus; color: (!!input.selection && input.selection.contains(input.rowIndex)) ? Qt.alpha(input.host.accent, 0.13) : "transparent"; border.width: input.activeFocus ? 1 : 0; border.color: input.host.accent }
    Rectangle {
        visible: !!input.selection && input.selection.active
        x: 5; y: 5; width: 20; height: 20; radius: 4
        color: (!!input.selection && input.selection.contains(input.rowIndex)) ? input.host.accent : input.host.panel
        border.width: 1; border.color: input.host.muted
        Text { anchors.centerIn: parent; text: (!!input.selection && input.selection.contains(input.rowIndex)) ? "✓" : ""; color: input.host.foreground(input.host.accent); font.pixelSize: 14 }
    }
    MouseArea {
        enabled: !input.mobile || (!!input.selection && input.selection.active)
        anchors.fill: parent
        propagateComposedEvents: true
        scrollGestureEnabled: false
        onWheel: wheel => { wheel.accepted = false }
        preventStealing: false
        property bool held: false
        onPressed: mouse => {
            held = false
            if (!input.mobile && !input.selection.active && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) mouse.accepted = false
        }
        onPressAndHold: mouse => {
            if (!input.mobile) return
            held = true; input.selection.choose(input.rowIndex); input.forceActiveFocus()
        }
        onClicked: mouse => {
            if (held) return
            if (input.selection.active || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                input.selection.choose(input.rowIndex, mouse.modifiers); input.forceActiveFocus()
            } else mouse.accepted = false
        }
    }
}
