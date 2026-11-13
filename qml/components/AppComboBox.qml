import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ComboBox {
    id: control
    implicitHeight: 44
    leftPadding: 14; rightPadding: 36
    font.pixelSize: 14
    hoverEnabled: true
    Accessible.name: displayText
    background: Rectangle {
        radius: 12
        color: control.palette.button
        border.color: control.activeFocus ? control.palette.highlight : control.palette.mid
        border.width: control.activeFocus ? 2 : 1
    }
    indicator: Canvas {
        x: control.width - width - 16; anchors.verticalCenter: parent.verticalCenter
        width: 12; height: 8
        property color ink: control.palette.windowText
        onInkChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset()
            c.strokeStyle = ink; c.lineWidth = 1.5; c.lineCap = "round"; c.lineJoin = "round"
            c.beginPath(); c.moveTo(1,1); c.lineTo(6,6); c.lineTo(11,1); c.stroke()
        }
    }
    contentItem: Text {
        text: control.displayText; font: control.font; color: control.palette.buttonText
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    delegate: Basic.ItemDelegate {
        required property int index
        required property var modelData
        width: control.width; height: 44
        highlighted: control.highlightedIndex === index
        text: String(modelData)
        contentItem: Text { text: parent.text; color: control.palette.windowText; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { radius: 8; color: parent.highlighted ? control.palette.mid : control.palette.button }
    }
    popup: Basic.Popup {
        y: control.height + 6
        width: control.width; padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 320)
        background: Rectangle { radius: 14; color: control.palette.button; border.color: control.palette.mid }
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            Basic.ScrollIndicator.vertical: Basic.ScrollIndicator { }
        }
    }
}
