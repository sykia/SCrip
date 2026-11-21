import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml" as Scrip

TestCase {
    name: "SwipeDismiss"
    when: windowShown
    visible: true
    width: 360; height: 520
    Component {
        id: fixture
        Window {
            id: root
            width: 360; height: 520; visible: true
            property alias popup: sheet
            property alias gesture: gesture
            property alias viewport: viewport
            property bool guarded: false
            property int dismissRequests: 0
            property int buttonClicks: 0
            Popup {
                id: sheet
                x: 20; y: 100; width: 320; height: 380; padding: 10
                modal: true
                background: Rectangle { color: "white" }
                contentItem: Item {
                    Rectangle { width: parent.width; height: 48; color: "gray" }
                    Button { x: 210; width: 70; height: 48; text: "Action"; onClicked: root.buttonClicks++ }
                    Flickable {
                        id: viewport
                        y: 58; width: parent.width; height: parent.height - y
                        contentWidth: width; contentHeight: 1800
                        boundsBehavior: Flickable.StopAtBounds
                    }
                }
                Scrip.SwipeDismiss {
                    id: gesture
                    targetPopup: sheet
                    dismissAction: function() {
                        root.dismissRequests++
                        if (!root.guarded) sheet.close()
                    }
                }
            }
        }
    }
    function opened() {
        const root = createTemporaryObject(fixture, this)
        root.popup.open()
        tryCompare(root.popup, "opened", true)
        return root
    }
    function press(root, x, y) { touchEvent(root.contentItem).press(0, root.contentItem, x, y).commit() }
    function move(root, x, y, delay) {
        touchEvent(root.contentItem).move(0, root.contentItem, x, y).commit()
        wait(delay || 0)
    }
    function release(root, x, y) { touchEvent(root.contentItem).release(0, root.contentItem, x, y).commit() }
    function test_shortPullReversesAndReturns() {
        const root = opened()
        press(root, 160, 125)
        move(root, 160, 150, 20)
        move(root, 160, 175, 20)
        verify(root.gesture.offset > 0)
        const offset = root.gesture.offset
        wait(100)
        compare(root.gesture.offset, offset)
        move(root, 160, 165, 20)
        verify(Math.abs(root.gesture.offset - offset + 10) < 1)
        wait(150)
        release(root, 160, 165)
        tryCompare(root.gesture, "offset", 0)
        verify(root.popup.visible)
    }
    function test_surfaceFollowsAndDismisses() {
        const root = opened()
        const top = root.popup.background.mapToItem(root.contentItem, 0, 0).y
        press(root, 160, 125)
        move(root, 160, 150, 20)
        move(root, 160, 305, 20)
        verify(root.gesture.offset > 100)
        const moved = root.popup.background.mapToItem(root.contentItem, 0, 0).y - top
        verify(Math.abs(moved - root.gesture.offset) < 1)
        wait(150)
        release(root, 160, 305)
        tryCompare(root.popup, "visible", false)
        compare(root.dismissRequests, 1)
    }
    function test_bodyPressStopsSettling() {
        const root = opened()
        press(root, 160, 125)
        move(root, 160, 150, 20)
        move(root, 160, 260, 20)
        wait(150)
        release(root, 160, 260)
        wait(16)
        press(root, 160, 450)
        const held = root.gesture.offset
        wait(100)
        compare(root.gesture.offset, held)
        release(root, 160, 450)
        tryCompare(root.popup, "visible", false)
    }
    function test_headerButtonStillClicks() {
        const root = opened()
        mouseClick(root.contentItem, 280, 125)
        compare(root.buttonClicks, 1)
        const touch = touchEvent(root.contentItem)
        touch.press(0, root.contentItem, 280, 125).commit()
        touch.release(0, root.contentItem, 280, 125).commit()
        compare(root.buttonClicks, 2)
        verify(root.popup.visible)
        compare(root.gesture.offset, 0)
    }
    function test_listOwnsScrolling() {
        const root = opened()
        mouseDrag(root.contentItem, 160, 390, 0, -120, Qt.LeftButton, Qt.NoModifier, 100)
        verify(root.viewport.contentY > 0)
        verify(root.popup.visible)
        compare(root.gesture.offset, 0)
    }
    function test_guardedDismissRestoresSurface() {
        const root = opened()
        root.guarded = true
        press(root, 160, 125)
        move(root, 160, 150, 20)
        move(root, 160, 305, 20)
        wait(150)
        release(root, 160, 305)
        tryCompare(root, "dismissRequests", 1)
        verify(root.popup.visible)
        compare(root.gesture.offset, 0)
    }
}
