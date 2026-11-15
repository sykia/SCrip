import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml" as Scrip

TestCase {
    name: "Motion"
    when: windowShown
    width: 320; height: 240
    Component {
        id: fixture
        Window {
            width: 320; height: 240; visible: true
            property int motion: 120
            property alias button: button
            property alias feedback: feedback
            property alias popup: popup
            AbstractButton {
                id: button
                width: 120; height: 48; hoverEnabled: true
                Scrip.InteractionFeedback { id: feedback }
            }
            Popup {
                id: popup
                opacity: 0; scale: 0.985
                width: 200; height: 100
                enter: Scrip.SurfaceTransition { duration: 120 }
                exit: Scrip.SurfaceTransition { duration: 120; leaving: true }
            }
        }
    }
    SignalSpy { id: clicks; signalName: "clicked" }
    function test_feedbackPreservesPointerClick() {
        const root = createTemporaryObject(fixture, this)
        verify(root)
        clicks.target = root.button
        clicks.clear()
        mouseClick(root.button, 60, 24)
        compare(clicks.count, 1)
        clicks.target = null
    }
    function test_feedbackAndReducedMotion() {
        const root = createTemporaryObject(fixture, this)
        verify(root)
        root.button.down = true
        tryCompare(root.feedback, "opacity", 0.14)
        root.button.enabled = false
        tryCompare(root.feedback, "opacity", 0)
        root.motion = 0
        root.button.enabled = true
        root.button.down = true
        wait(1)
        compare(root.feedback.opacity, 0.14)
        root.button.down = false
        wait(1)
        verify(root.feedback.opacity < 0.14)
    }
    function test_popupReversalKeepsCurrentOpacity() {
        const root = createTemporaryObject(fixture, this)
        verify(root)
        root.popup.open()
        wait(40)
        const openingOpacity = root.popup.opacity
        verify(openingOpacity > 0 && openingOpacity < 1)
        root.popup.close()
        verify(Math.abs(root.popup.opacity - openingOpacity) < 0.1)
        wait(20)
        const closingOpacity = root.popup.opacity
        root.popup.open()
        verify(Math.abs(root.popup.opacity - closingOpacity) < 0.1)
        tryCompare(root.popup, "opened", true)
        tryCompare(root.popup, "opacity", 1)
    }
    function test_popupRapidReopenRestoresEndpoints() {
        const root = createTemporaryObject(fixture, this)
        verify(root)
        for (let i = 0; i < 3; ++i) {
            root.popup.open()
            wait(25)
            root.popup.close()
            wait(20)
        }
        root.popup.open()
        tryCompare(root.popup, "opened", true)
        tryCompare(root.popup, "opacity", 1)
        tryCompare(root.popup, "scale", 1)
        root.popup.close()
        tryCompare(root.popup, "visible", false)
    }
}
