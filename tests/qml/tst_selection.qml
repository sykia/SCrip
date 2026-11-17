import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml" as Scrip

TestCase {
    id: test
    name: "TrackSelection"
    when: windowShown
    visible: true
    width: 1100; height: 700
    readonly property var a: ({urn: "soundcloud:tracks:1", title: "First"})
    readonly property var b: ({urn: "soundcloud:tracks:2", title: "Second"})
    QtObject {
        id: mockHost
        property color ink: "#eeeeee"
        property color muted: "#aaaaaa"
        property color accent: "#b291ff"
        property color panel: "#202020"
        property color raised: "#303030"
        property bool dark: true
        property int motion: 0
        function foreground(color) { return "#000000" }
    }
    QtObject {
        id: mockBackend
        property var catalogState: ({})
        property bool signedIn: true
        signal selectionSaved(string token, bool success, string error)
        function enqueueTracks(tracks) { return "" }
        function listDestinations() {}
    }
    Component { id: controller; Scrip.TrackSelection {} }
    Component {
        id: barFixture
        Rectangle {
            color: mockHost.panel
            height: panel.implicitHeight
            property alias selection: panel.selection
            property alias mobile: panel.mobile
            function back() { return panel.back() }
            Scrip.SelectionBar { id: panel; width: parent.width; host: mockHost; backend: mockBackend }
        }
    }
    Component {
        id: rowFixture
        Item {
            id: testRow
            property int wheels: 0
            WheelHandler { target: null; onWheel: event => { testRow.wheels++; event.accepted = true } }
            width: 300; height: 64
            property int clicks: 0
            property alias input: input
            required property var controller
            property bool mobile: false
            MouseArea { anchors.fill: parent; onClicked: parent.clicks++ }
            Scrip.SelectionInput { id: input; anchors.fill: parent; host: mockHost; rowIndex: 0; selection: parent.controller; mobile: parent.mobile }
        }
    }
    Component {
        id: mobileListFixture
        ListView {
            id: list
            width: 300; height: 240; clip: true
            required property var controller
            property int clicks: 0
            model: list.controller.rows
            delegate: ItemDelegate {
                required property int index
                width: list.width; height: 64
                onPressAndHold: list.controller.choose(index)
                onClicked: if (!list.controller.active) list.clicks++
                Scrip.SelectionInput { anchors.fill: parent; mobile: true; host: mockHost; selection: list.controller; rowIndex: parent.index }
            }
        }
    }
    function make() { return createTemporaryObject(controller, test, {rows: [a, b, a, {urn: "soundcloud:playlists:1"}, b]}) }
    function test_orderDuplicatesRange() {
        const s = make()
        s.choose(2); s.choose(0, Qt.ControlModifier)
        compare(s.count, 2); compare(s.tracks(), [a, a])
        s.choose(4, Qt.ShiftModifier)
        compare(s.count, 4); compare(s.tracks(), [a, b, a, b])
        s.choose(2); compare(s.tracks(), [a, b, b])
        s.reset(); s.allLoaded(); compare(s.count, 4)
    }
    function test_paginationAndContext() {
        const s = make(); s.choose(2)
        s.rows = s.rows.concat([a, b])
        compare(s.count, 1); verify(s.contains(2)); verify(!s.contains(5))
        s.allLoaded(); compare(s.count, 6)
        s.context = "another search"; compare(s.count, 0); verify(!s.active)
        s.choose(0); s.rows = [b, a]; compare(s.count, 0)
        s.choose(0); s.context = "another account"; compare(s.count, 0)
    }
    function test_failureBusyAndStaleReplies() {
        const s = make(); s.choose(0); s.begin("old")
        s.choose(1); s.allLoaded(); compare(s.count, 1)
        s.finish("old", false, "Quota"); compare(s.count, 1); compare(s.message, "Quota"); verify(!s.busy)
        s.begin("old-again"); s.context = "next"; s.choose(1); s.begin("new")
        s.finish("old-again", true, ""); verify(s.busy); compare(s.count, 1)
        s.finish("new", true, ""); compare(s.count, 0); compare(s.message, "Добавлено")
    }
    function test_pointerAndKeyboard() {
        const s = make()
        const row = createTemporaryObject(rowFixture, test, {controller: s})
        mouseClick(row, 80, 32); compare(row.clicks, 1); compare(s.count, 0)
        mouseClick(row, 80, 32, Qt.LeftButton, Qt.ControlModifier); compare(row.clicks, 1); compare(s.count, 1)
        keyClick(Qt.Key_Space); compare(s.count, 0)
        keyClick(Qt.Key_Escape); verify(!s.active)
        mouseWheel(row, 80, 32, 0, -120)
        compare(row.wheels, 1)
    }
    function test_mobileHoldAndScroll() {
        const s = make()
        s.rows = Array.from({length: 30}, (_, i) => ({urn: "soundcloud:tracks:" + i, title: "Track " + i}))
        const list = createTemporaryObject(mobileListFixture, test, {controller: s})
        wait(30)
        compare(list.count, 30)
        mouseClick(list, 100, 32); compare(list.clicks, 1); compare(s.count, 0)
        mousePress(list, 100, 32)
        wait(1000)
        mouseRelease(list, 100, 32)
        compare(s.count, 1); compare(list.clicks, 1)
        s.reset()
        mousePress(list, 100, 200)
        mouseMove(list, 100, 170, 20)
        mouseMove(list, 100, 80, 20)
        mouseMove(list, 100, 30, 20)
        mouseRelease(list, 100, 30)
        verify(list.contentY > 0); compare(s.count, 0); verify(!s.active)
    }
    function test_backendResultOnlyAffectsCurrentOperation() {
        const s = make(); s.choose(0)
        const bar = createTemporaryObject(barFixture, test, {width: 600, selection: s})
        s.begin("first"); mockBackend.selectionSaved("first", false, "API error")
        compare(s.count, 1); compare(s.message, "API error")
        s.begin("first-retry"); s.context = "next list"; s.choose(1); s.begin("second")
        mockBackend.selectionSaved("first-retry", true, "")
        compare(s.count, 1); verify(s.busy)
        mockBackend.selectionSaved("second", true, "")
        compare(s.count, 0); verify(!s.active)
    }
    function test_layout_data() { return [{tag: "phone320", width: 280, mobile: true}, {tag: "phone412", width: 372, mobile: true}, {tag: "desktop", width: 1000, mobile: false}] }
    function test_layout(data) {
        const s = make(); s.allLoaded()
        const bar = createTemporaryObject(barFixture, test, {width: data.width, selection: s, mobile: data.mobile})
        verify(bar); wait(30)
        verify(bar.height > 0); verify(bar.height <= (data.mobile ? 100 : 50))
        const flow = findChild(bar, "selectionControls")
        for (const child of flow.children) {
            if (!child.visible) continue
            verify(child.x >= 0); verify(child.x + child.width <= data.width + 1)
            verify(child.height >= (data.mobile ? 44 : 36))
        }
        grabImage(bar).save("selection-" + data.tag + ".png")
        s.begin("busy"); verify(bar.back()); verify(s.active)
        s.finish("busy", false, "Failure"); verify(bar.back()); verify(!s.active)
    }
}
