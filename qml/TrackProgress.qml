import QtQuick
import QtQuick.Controls.Basic
import SCrip.Native 1.0

Control {
    id: root
    property url source
    property string selectionKey
    property real duration: 0
    property real position: 0
    readonly property real from: 0
    readonly property real to: duration
    readonly property real stepSize: 5000
    property color ink: "#777777"
    property color accent: "#bbbbbb"
    property bool adjusting: false
    property real preview: 0
    readonly property real value: adjusting ? preview : position
    readonly property bool waveformReady: wave.ready
    signal seekRequested(real milliseconds)
    implicitHeight: 52
    implicitWidth: 160
    enabled: duration > 0
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("Позиция воспроизведения")
    Accessible.description: Math.round(value / 1000) + qsTr(" из ") + Math.round(duration / 1000) + qsTr(" секунд. Стрелки: 5 секунд, Home и End: начало и конец")
    Accessible.onIncreaseAction: seekTo(value + stepSize)
    Accessible.onDecreaseAction: seekTo(value - stepSize)
    onSelectionKeyChanged: { adjusting = false; pointer.validGesture = false }
    function seekTo(value) { if (enabled) seekRequested(Math.max(0, Math.min(duration, value))) }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Down) seekTo(value - 5000)
        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Up) seekTo(value + 5000)
        else if (event.key === Qt.Key_Home) seekTo(0)
        else if (event.key === Qt.Key_End) seekTo(duration)
        else return
        event.accepted = true
    }
    background: Item {
        TrackWaveform {
            id: wave; anchors.fill: parent
            source: root.visible ? root.source : ""
            detailed: pointer.pressed && pointer.validGesture
            ink: root.ink; accent: root.accent
            progress: root.duration > 0 ? root.value / root.duration : 0
        }
        Rectangle {
            visible: !wave.ready; anchors.verticalCenter: parent.verticalCenter
            width: parent.width; height: 3; radius: 2; color: root.ink
            Rectangle { width: parent.width * Math.max(0, Math.min(1, root.duration > 0 ? root.value / root.duration : 0)); height: parent.height; color: root.accent; radius: 2 }
        }
        Rectangle { anchors.fill: parent; color: "transparent"; border.width: root.visualFocus ? 1 : 0; border.color: root.accent; radius: 4 }
    }
    MouseArea {
        id: pointer; anchors.fill: parent
        property bool validGesture: false
        property real startX: 0
        property real startY: 0
        property string pressedKey
        preventStealing: false
        drag.target: dragTarget; drag.axis: Drag.XAxis
        Item { id: dragTarget }
        onPressed: mouse => { startX = mouse.x; startY = mouse.y; pressedKey = root.selectionKey; validGesture = true; root.forceActiveFocus() }
        onPositionChanged: mouse => {
            if (Math.abs(mouse.y - startY) > drag.threshold && Math.abs(mouse.y - startY) > Math.abs(mouse.x - startX)) { validGesture = false; root.adjusting = false }
            if (validGesture && drag.active) { root.adjusting = true; root.preview = Math.max(0, Math.min(root.duration, mouse.x / width * root.duration)) }
        }
        onReleased: mouse => {
            if (validGesture && pressedKey === root.selectionKey) root.seekTo(mouse.x / width * root.duration)
            validGesture = false; root.adjusting = false
        }
        onCanceled: { validGesture = false; root.adjusting = false }
    }
}
