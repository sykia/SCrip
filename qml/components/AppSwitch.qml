import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Switch {
    id: control
    implicitHeight: 48
    spacing: 12
    hoverEnabled: true
    Accessible.name: text
    indicator: Rectangle {
        implicitWidth: 44; implicitHeight: 26
        x: control.leftPadding; y: (control.height - height) / 2
        radius: 13
        color: control.checked ? control.palette.highlight : control.palette.mid
        border.width: control.activeFocus ? 2 : 0
        border.color: control.palette.highlight
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3; width: 20; height: 20; radius: 10
            color: control.checked ? control.palette.highlightedText : control.palette.windowText
        }
    }
    contentItem: Text {
        text: control.text; font.pixelSize: 14; color: control.palette.windowText
        verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap
        leftPadding: control.indicator.width + control.spacing
        opacity: control.enabled ? 1 : 0.45
    }
}
