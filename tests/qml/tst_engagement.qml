import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml" as Scrip

TestCase {
    id: test
    name: "EngagementAction"
    when: windowShown
    visible: true
    width: 412; height: 500
    Rectangle { anchors.fill: parent; color: "#141414"; z: -1 }
    QtObject {
        id: mockHost
        property color ink: "#eeeeee"
        property color muted: "#aaaaaa"
        property color raised: "#242424"
        property color accent: "#b291ff"
        property bool dark: true
        property int motion: 0
        property int opened: 0
        function foreground(color) { return "#000000" }
        function openEngagementPeople(item, kind) { opened++ }
    }
    QtObject {
        id: mockBackend
        readonly property var uiState: catalogState
        property var catalogState: ({})
        property bool signedIn: true
        property int writes: 0
        function loadEngagement(kind) {}
        function like(item) { writes++ }
        function toggleRepost(item) { writes++ }
    }
    Component {
        id: fixture
        Flickable {
            width: test.width; height: 300; contentHeight: 1000
            Scrip.EngagementAction {
                objectName: "action"; x: 50; y: 80
                host: mockHost; backend: mockBackend; mobile: true
                item: ({urn: "soundcloud:tracks:1", favoritings_count: 1234567890, user_favorite: false})
            }
        }
    }
    function init() { mockHost.opened = 0; mockBackend.writes = 0; mockBackend.catalogState = {} }
    function button() {
        const view = createTemporaryObject(fixture, test)
        verify(view)
        return findChild(view, "action")
    }
    function test_shortAndKeyboard() {
        const action = button()
        mouseClick(action)
        compare(mockBackend.writes, 1); compare(mockHost.opened, 0)
        action.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(mockBackend.writes, 2)
        keyClick(Qt.Key_F10, Qt.ShiftModifier)
        keyClick(Qt.Key_Down); keyClick(Qt.Key_Return)
        tryCompare(mockHost, "opened", 1)
        compare(mockBackend.writes, 2)
    }
    function test_holdDoesNotLike() {
        const action = button()
        mousePress(action, action.width / 2, action.height / 2)
        wait(1000)
        mouseRelease(action, action.width / 2, action.height / 2)
        compare(mockHost.opened, 1); compare(mockBackend.writes, 0)
    }
    function test_scrollCancelsHold() {
        const action = button()
        mousePress(action, action.width / 2, action.height / 2)
        for (let i = 1; i <= 8; ++i) mouseMove(action, action.width / 2, action.height / 2 - i * 9, 20)
        wait(1000)
        mouseRelease(action, action.width / 2, -60)
        compare(mockHost.opened, 0); compare(mockBackend.writes, 0)
    }
    function test_touchScrollCancelsHold() {
        const action = button()
        const sequence = touchEvent(action)
        sequence.press(0, action, action.width / 2, action.height / 2).commit()
        for (let i = 1; i <= 8; ++i) {
            sequence.move(0, action, action.width / 2, action.height / 2 - i * 9).commit()
            wait(20)
        }
        wait(1000)
        sequence.release(0, action, action.width / 2, -60).commit()
        compare(mockHost.opened, 0); compare(mockBackend.writes, 0)
    }
    function test_layout_data() {
        return [{tag: "320", size: 320}, {tag: "412", size: 412}, {tag: "desktop", size: 1000}]
    }
    function test_layout(data) {
        test.width = data.size
        const action = button()
        verify(action.x + action.width <= test.width)
        verify(action.width >= 48); verify(action.height >= 44)
        verify(action.fullCountText.length > action.countText.length)
        wait(40)
        verify(!findChild(action, "engagementCount").truncated)
        grabImage(test).save("engagement-" + data.tag + ".png")
        action.repost = true
        mockBackend.catalogState = {engagementReady: {trackReposts: true}}
        mouseClick(action); compare(mockBackend.writes, 1)
        mousePress(action); wait(1000); mouseRelease(action)
        compare(mockHost.opened, 1); compare(mockBackend.writes, 1)
    }
    function test_resourceChangesDuringPress() {
        const action = button()
        mousePress(action)
        action.item = {urn: "soundcloud:tracks:2"}
        mouseRelease(action)
        compare(mockBackend.writes, 0); compare(mockHost.opened, 0)
    }
    function test_countsAndPending() {
        const action = button()
        verify(action.countKnown)
        verify(action.width <= 220); verify(action.height >= 44)
        mockBackend.catalogState = {engagementCounts: {"soundcloud:tracks:1": {favoritings_count: 0}}, engagementStates: {"soundcloud:tracks:1": {like: true}}, likePending: {"soundcloud:tracks:1": true}}
        compare(action.countText, "0"); verify(action.selected)
        mouseClick(action); compare(mockBackend.writes, 0)
        action.item = {urn: "soundcloud:tracks:1", reveal_stats: false, favoritings_count: 7}
        compare(action.countText, "—")
        mockBackend.catalogState = {}
        action.item = {urn: "soundcloud:tracks:2"}
        compare(action.countText, "—")
        action.item = {urn: "soundcloud:playlists:1", likes_count: 0}
        verify(!action.hasPeople); compare(action.countText, "0")
        action.repost = true; verify(action.hasPeople)
        action.item = {urn: "soundcloud:system-playlists:1"}; verify(!action.visible)
    }
}
