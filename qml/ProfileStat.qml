import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: control
    required property var host
    property real value: 0
    property string caption: ""
    implicitWidth: Math.max(60, Math.min(100, Math.max(count.implicitWidth, label.implicitWidth) + 8))
    implicitHeight: 48
    hoverEnabled: true
    Accessible.name: caption + ": " + value
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    background: Rectangle { radius: 8; color: Qt.alpha(control.host.ink, control.down || control.hovered ? 0.04 : 0) }
    contentItem: Column {
        spacing: 4
        Text { id: count; anchors.horizontalCenter: parent.horizontalCenter; text: Number(control.value).toLocaleString(Qt.locale(), "f", 0); color: control.hovered ? control.host.accent : control.host.ink; font.pixelSize: 16; font.weight: Font.DemiBold; textFormat: Text.PlainText }
        Text { id: label; anchors.horizontalCenter: parent.horizontalCenter; text: control.caption; color: control.host.muted; font.pixelSize: 11; textFormat: Text.PlainText }
    }
}
