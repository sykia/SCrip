import QtQuick

// Bounded surfaces settle to an endpoint; lists retain Flickable's free inertia.
// Values and velocity are pixels and pixels/second, independent of refresh rate.
QtObject {
    id: motion
    property real value: 0
    property real extent: 1
    property real velocity: 0
    property real destination: 0
    readonly property bool running: frames.running
    signal settled(bool closed)
    function stop() { frames.stop(); velocity = 0 }
    function settle(releaseVelocity, threshold) {
        velocity = releaseVelocity
        // Project a short distance using the *signed* release velocity.
        const boundary = threshold === undefined ? Math.min(100, extent * 0.15) : threshold
        destination = value + velocity * 0.18 > boundary ? extent : 0
        frames.start()
    }
    function advance(dt) {
        // Exact critically damped spring solution: no Euler instability or
        // duration/easing restart when the finger changes speed or reverses.
        const omega = 18
        const displacement = value - destination
        const c = velocity + omega * displacement
        const decay = Math.exp(-omega * dt)
        const next = destination + (displacement + c * dt) * decay
        velocity = (velocity - omega * c * dt) * decay
        value = Math.max(0, Math.min(extent, next))
        if ((value === 0 && velocity < 0) || (value === extent && velocity > 0)
                || (Math.abs(value - destination) < 0.25 && Math.abs(velocity) < 5)) {
            value = destination
            frames.stop()
            velocity = 0
            settled(destination === extent)
        }
    }
    property FrameAnimation frames: FrameAnimation {
        onTriggered: motion.advance(frameTime)
    }
}
