import QtQuick
import QtQuick.Controls

Button {
    id: control
    implicitHeight: 40
    implicitWidth: label.implicitWidth + 28
    padding: 12
    hoverEnabled: true
    font.pixelSize: 14
    Accessible.name: text
    background: Rectangle { radius: 12; color: control.down ? Qt.darker(control.palette.button, 1.12) : control.palette.button; opacity: control.enabled ? 1 : 0.5 }
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
