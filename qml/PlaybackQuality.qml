import QtQuick
import QtQuick.Controls

Control {
    id: control
    property var segments: ["Стандартное", "Экономия трафика"]
    property bool stacked: false
    property int value: 0
    property color ink: "#eeeeee"
    property color surface: "#292929"
    property color accent: "#f43756"
    property color selectedInk: "white"
    property int motion: 150
    signal edited(int value)
    implicitWidth: 320
    implicitHeight: 44
    padding: 4
    Accessible.role: Accessible.Grouping
    Accessible.name: "Качество воспроизведения"
    background: Rectangle {
        radius: 12
        color: control.surface
        Rectangle {
            x: 4 + (control.stacked ? 0 : control.value * (parent.width - 8) / control.segments.length)
            y: 4 + (control.stacked ? control.value * (parent.height - 8) / control.segments.length : 0)
            width: (parent.width - 8) / (control.stacked ? 1 : control.segments.length)
            height: (parent.height - 8) / (control.stacked ? control.segments.length : 1)
            radius: 9
            color: control.accent
            Behavior on x { NumberAnimation { duration: control.motion; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {
        Repeater {
            model: control.segments
            RadioButton {
                id: segment
                required property int index
                required property string modelData
                objectName: "qualityOption" + index
                x: control.stacked ? 0 : index * control.availableWidth / control.segments.length
                y: control.stacked ? index * control.availableHeight / control.segments.length : 0
                width: control.availableWidth / (control.stacked ? 1 : control.segments.length)
                height: control.availableHeight / (control.stacked ? control.segments.length : 1)
                text: modelData
                checked: control.value === index
                autoExclusive: true
                hoverEnabled: true
                padding: 6
                Accessible.name: modelData
                onClicked: control.edited(index)
                indicator: Item {}
                background: Rectangle {
                    radius: 9
                    color: segment.down ? Qt.alpha(control.ink, 0.12) : segment.hovered && !segment.checked ? Qt.alpha(control.ink, 0.06) : "transparent"
                    border.width: segment.visualFocus ? 2 : 0
                    border.color: control.ink
                }
                contentItem: Text {
                    text: segment.text
                    font.pixelSize: 12
                    font.weight: segment.checked ? Font.DemiBold : Font.Normal
                    color: segment.checked ? control.selectedInk : control.ink
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
