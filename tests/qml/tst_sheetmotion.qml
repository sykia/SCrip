import QtQuick
import QtTest
import "../../qml" as Scrip

TestCase {
    name: "SheetMotion"
    when: windowShown
    Component { id: fixture; Scrip.SheetMotion { extent: 800 } }
    function test_releaseUsesSignedVelocity() {
        const slow = createTemporaryObject(fixture, this)
        slow.value = 100; slow.settle(0)
        compare(slow.destination, 0)
        const fast = createTemporaryObject(fixture, this)
        fast.value = 100; fast.settle(1200)
        compare(fast.destination, 800)
        compare(fast.velocity, 1200)
        fast.advance(0.016)
        verify(fast.value > 100)
        const reversed = createTemporaryObject(fixture, this)
        reversed.value = 300; reversed.settle(-1200)
        compare(reversed.destination, 0)
        reversed.advance(0.016)
        verify(reversed.value < 300)
    }
    function test_stopPreservesPosition() {
        const motion = createTemporaryObject(fixture, this)
        motion.value = 100; motion.settle(1200)
        tryVerify(() => motion.value > 100)
        motion.stop()
        const held = motion.value
        wait(80)
        compare(motion.value, held)
        compare(motion.running, false)
    }
    function test_refreshRateIndependent() {
        const a = createTemporaryObject(fixture, this)
        const b = createTemporaryObject(fixture, this)
        a.value = b.value = 150
        a.settle(900); b.settle(900)
        for (let i = 0; i < 6; ++i) a.advance(1 / 60)
        for (let i = 0; i < 14; ++i) b.advance(1 / 140)
        verify(Math.abs(a.value - b.value) < 0.01)
        verify(Math.abs(a.velocity - b.velocity) < 0.01)
        a.stop(); b.stop()
    }
    function test_endpoints() {
        const motion = createTemporaryObject(fixture, this)
        motion.value = 30; motion.settle(0)
        tryCompare(motion, "running", false)
        compare(motion.value, 0)
        motion.value = 250; motion.settle(1000)
        tryCompare(motion, "running", false)
        compare(motion.value, 800)
    }
}
