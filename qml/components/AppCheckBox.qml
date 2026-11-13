import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.CheckBox {
    id: control
    implicitHeight: 40
    spacing: 12
    hoverEnabled: true
    Accessible.name: text
    indicator: Rectangle {
        implicitWidth: 20; implicitHeight: 20
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 6
        color: control.checked ? control.palette.highlight : control.palette.base
        border.color: control.activeFocus ? control.palette.highlight : control.checked ? control.palette.highlight : control.palette.mid
        border.width: control.activeFocus ? 2 : 1
        Text { anchors.centerIn: parent; text: "✓"; visible: control.checked; color: control.palette.highlightedText; font.pixelSize: 15; font.bold: true }
    }
    contentItem: Text {
        text: control.text; color: control.palette.windowText
        font.pixelSize: 14; wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
        opacity: control.enabled ? 1 : 0.45
    }
}
