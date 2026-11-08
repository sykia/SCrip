import QtQuick
import QtQuick.Controls

Button {
    id: control
    implicitHeight: 40
    implicitWidth: label.implicitWidth + 28
    padding: 12
    Accessible.name: text
    background: Rectangle { radius: 14; color: control.down ? "#ded5df" : "#ede7ee"; opacity: control.enabled ? 1 : 0.5 }
    contentItem: Label {
        id: label
        text: control.text
        textFormat: Text.PlainText
        color: "#242426"
        opacity: control.enabled ? 1 : 0.5
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
