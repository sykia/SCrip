import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    required property var host
    property bool mobile: false
    property bool primary: false
    property bool destructive: false
    property bool selected: false
    property bool quiet: false
    implicitHeight: mobile ? 44 : 36
    implicitWidth: Math.max(mobile ? 44 : 36, label.implicitWidth + 28)
    leftPadding: 14; rightPadding: 14
    hoverEnabled: true
    Accessible.name: text
    opacity: enabled ? 1 : 0.45
    scale: down && host.motion > 0 ? 0.975 : 1
    Behavior on opacity { NumberAnimation { duration: control.host.motion * 0.5; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: control.host.motion * (control.down ? 0.25 : 0.65); easing.type: Easing.OutCubic } }
    InteractionFeedback {}
    contentItem: Text {
        id: label
        text: control.text; textFormat: Text.PlainText
        font.pixelSize: 13; font.weight: Font.Medium
        color: control.primary ? control.host.foreground(control.host.accent) : control.destructive ? (control.host.dark ? "#ff8c8c" : "#bb3535") : control.selected ? control.host.accent : control.host.ink
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        Behavior on color { ColorAnimation { duration: control.host.motion * 0.5 } }
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 10
        color: control.primary ? control.host.accent : control.down || control.hovered ? Qt.alpha(control.host.ink, 0.12) : control.selected ? Qt.alpha(control.host.accent, 0.12) : control.quiet ? "transparent" : control.host.raised
        Behavior on color { ColorAnimation { duration: control.host.motion > 0 ? 120 : 0 } }
    }
}
