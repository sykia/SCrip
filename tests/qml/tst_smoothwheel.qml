import QtQuick
import QtTest
import "../../qml" as Scrip

TestCase {
    name: "SmoothWheel"
    visible: true
    width: 320; height: 240
    when: windowShown
    Component {
        id: fixture
        Flickable {
            id: view
            width: 200; height: 100
            contentWidth: width; contentHeight: 1000
            topMargin: 20; bottomMargin: 30
            property alias wheel: wheel
            Scrip.SmoothWheel { id: wheel; flickable: view; duration: 0 }
        }
    }
    function test_mouseWheelIsHandledOnce() {
        const view = createTemporaryObject(fixture, this)
        view.contentY = 0
        waitForRendering(view)
        mouseWheel(view, 60, 50, 0, -120)
        tryCompare(view, "contentY", 176)
    }
    function test_boundsAndMargins() {
        const view = createTemporaryObject(fixture, this)
        verify(view)
        view.wheel.scroll(12000)
        compare(view.contentY, -20)
        view.wheel.scroll(-12000)
        compare(view.contentY, 930)
    }
    function test_directionReversal() {
        const view = createTemporaryObject(fixture, this)
        view.contentY = 400
        view.wheel.duration = 120
        view.wheel.scroll(-120)
        tryVerify(() => view.contentY > 400)
        const position = view.contentY
        view.wheel.scroll(120)
        compare(view.wheel.destination, position - 176)
        tryCompare(view, "contentY", position - 176)
    }
    function test_resizeCancelsOldDestination() {
        const view = createTemporaryObject(fixture, this)
        view.wheel.duration = 120
        view.wheel.scroll(-120)
        view.contentHeight = 50
        view.returnToBounds()
        tryVerify(() => Math.abs(view.contentY + 20) < 1)
    }
}
