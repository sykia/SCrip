import QtQuick

// Animate discrete mouse wheels only; preserve native touch and touchpad inertia.
WheelHandler {
    id: wheel
    required property Flickable flickable
    property int duration: 180
    property real step: 176
    property real destination: 0
    // Android uses Flickable for every input source, including external mice.
    enabled: Qt.platform.os !== "android"
    acceptedDevices: PointerDevice.Mouse
    target: null

    function scroll(delta) {
        const minimum = flickable.originY - flickable.topMargin
        const maximum = Math.max(minimum, flickable.originY + flickable.contentHeight - flickable.height + flickable.bottomMargin)
        const continuing = motion.running && (destination - flickable.contentY) * delta < 0
        const base = continuing ? destination : flickable.contentY
        destination = Math.max(minimum, Math.min(maximum, base - delta / 120 * step))
        flickable.cancelFlick()
        if (duration > 0) {
            motion.to = destination
            motion.restart()
        } else {
            motion.stop()
            flickable.contentY = destination
        }
    }
    onWheel: event => {
        if (event.pixelDelta.y !== 0 || event.angleDelta.y === 0 || event.modifiers !== Qt.NoModifier) {
            motion.stop()
            event.accepted = false
            return
        }
        scroll(event.angleDelta.y)
        event.accepted = true
    }
    property SmoothedAnimation motion: SmoothedAnimation {
        id: motion
        target: wheel.flickable
        property: "contentY"
        velocity: -1
        duration: wheel.duration
        reversingMode: SmoothedAnimation.Immediate
    }
    property PointHandler interruption: PointHandler {
        parent: wheel.flickable
        onActiveChanged: if (active) motion.stop()
    }
    property Connections movementConnections: Connections {
        target: wheel.flickable
        function onMovementStarted() { motion.stop() }
        function onContentHeightChanged() { motion.stop() }
        function onHeightChanged() { motion.stop() }
        function onVisibleChanged() { motion.stop() }
    }
}
