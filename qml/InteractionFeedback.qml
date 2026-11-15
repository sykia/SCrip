import QtQuick
import QtQuick.Controls

// Input-neutral feedback: AbstractButton.down includes touch and keyboard presses.
Rectangle {
    id: feedback
    readonly property var control: parent
    readonly property var hostWindow: control.Window.window
    readonly property int duration: hostWindow && hostWindow.motion !== undefined ? hostWindow.motion : 0
    readonly property bool focused: control.visualFocus === true
    anchors.fill: parent
    radius: Math.min(12, height / 2)
    z: 10
    HoverHandler { id: pointerHover; enabled: feedback.control.enabled }
    color: control.palette.highlight
    opacity: !control.enabled ? 0 : control.down ? 0.14 : focused ? 0.09 : (control.hovered || pointerHover.hovered) ? 0.055 : 0
    border.width: focused ? 1 : 0
    border.color: control.palette.highlight
    Behavior on opacity {
        NumberAnimation { duration: feedback.control.down ? feedback.duration * 0.28 : feedback.duration * 0.65; easing.type: Easing.OutCubic }
    }
}
