import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml" as Scrip

TestCase {
    id: test
    name: "TrackProgress"
    when: windowShown
    width: 420; height: 500; visible: true
    Component {
        id: fixture
        Flickable {
            width: test.width; height: 400; contentHeight: 1200
            Scrip.TrackProgress {
                id: progress; objectName: "progress"
                x: 10; y: 100; width: parent.width - 20; height: 52
                duration: 100000; position: 20000; selectionKey: "track-a"
                property int requests: 0
                property real target: -1
                onSeekRequested: milliseconds => { target = milliseconds; requests++ }
            }
        }
    }
    function setup() {
        const view = createTemporaryObject(fixture, test)
        verify(view)
        return {view: view, progress: findChild(view, "progress")}
    }
    function test_fallbackTapDragKeyboard() {
        const p = setup().progress
        verify(!p.waveformReady)
        mouseClick(p, p.width * 0.75, p.height/2)
        compare(p.requests, 1); verify(Math.abs(p.target - 75000) < 100)
        // Drag previews without sending a request on every pointer move.
        mousePress(p, p.width * 0.75, p.height/2)
        mouseMove(p, p.width * 0.25, p.height/2, 30)
        compare(p.requests, 1)
        mouseRelease(p, p.width * 0.25, p.height/2)
        compare(p.requests, 2); verify(Math.abs(p.target - 25000) < 100)
        p.forceActiveFocus(); keyClick(Qt.Key_Right); compare(p.target, 25000)
        keyClick(Qt.Key_Left); compare(p.target, 15000)
        keyClick(Qt.Key_Home); compare(p.target, 0)
        keyClick(Qt.Key_End); compare(p.target, 100000)
        p.duration = 0; keyClick(Qt.Key_Left); compare(p.requests, 6)
    }
    function test_trackChangesDuringDrag() {
        const p = setup().progress
        mousePress(p, 250, 26); mouseMove(p, 100, 26, 30)
        p.selectionKey = "track-b"
        mouseRelease(p, 100, 26)
        compare(p.requests, 0); verify(!p.adjusting)
    }
    function test_verticalMouseGesture() {
        const p = setup().progress
        mousePress(p, 150, 26); mouseMove(p, 151, 150, 30); mouseRelease(p, 151, 150)
        compare(p.requests, 0)
    }
    function test_touchScrollDoesNotSeek() {
        const state = setup(), p = state.progress
        const touch = touchEvent(state.view)
        touch.press(0, p, 150, 26).commit(); wait(30)
        touch.move(0, p, 150, 0).commit(); wait(20)
        touch.move(0, p, 150, -60).commit(); wait(20)
        touch.release(0, p, 150, -80).commit()
        wait(30); compare(p.requests, 0)
        tryVerify(function() { return state.view.contentY > 0 }, 1000)
    }
    function test_horizontalTouchSeek() {
        const state = setup(), p = state.progress
        const touch = touchEvent(state.view)
        touch.press(0, p, p.width * 0.75, 26).commit(); wait(20)
        touch.move(0, p, p.width * 0.5, 26).commit(); wait(20)
        touch.move(0, p, p.width * 0.25, 26).commit(); wait(20)
        compare(p.requests, 0)
        touch.release(0, p, p.width * 0.25, 26).commit(); wait(20)
        compare(p.requests, 1); verify(Math.abs(p.target - 25000) < 500)
    }
    function test_phoneAndDesktop() {
        for (const width of [320, 412, 1080]) {
            test.width = width
            const state = setup()
            verify(state.progress.width <= width)
            compare(state.progress.Accessible.name, "Позиция воспроизведения")
        }
        test.width = 420
    }
}
