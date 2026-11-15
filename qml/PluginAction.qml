import QtQuick
import QtQuick.Controls

Button {
    id: control
    readonly property int motionDuration: control.Window.window && control.Window.window.motion !== undefined ? control.Window.window.motion : 0
    scale: down && motionDuration > 0 ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: control.motionDuration * 0.5; easing.type: Easing.OutCubic } }
    hoverEnabled: true
    InteractionFeedback {}
    implicitHeight: 40
    implicitWidth: label.implicitWidth + 28
    padding: 12
    Accessible.name: text
    background: Rectangle { radius: 14; color: control.down ? Qt.darker(control.palette.button, 1.12) : control.palette.button; opacity: control.enabled ? 1 : 0.5; Behavior on color { ColorAnimation { duration: control.motionDuration * 0.5 } } }
    contentItem: Label {
        id: label
        text: control.text
        textFormat: Text.PlainText
        color: control.palette.buttonText
        opacity: control.enabled ? 1 : 0.5
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
