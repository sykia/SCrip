import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml" as Scrip

TestCase {
    id: test
    name: "TrackSearchFilters"
    when: windowShown
    visible: true
    width: 1100; height: 900
    Rectangle { anchors.fill: parent; color: "#141414"; z: -1 }
    QtObject {
        id: mockHost
        property color ink: "#eeeeee"
        property color muted: "#aaaaaa"
        property color raised: "#242424"
        property color accent: "#b291ff"
        property bool dark: true
        property int motion: 0
        function foreground(color) { return "#000000" }
    }
    QtObject {
        id: mockBackend
        property int requests: 0
        property var last: ({})
        function validateTrackFilters(values) { return values.durationFrom === "bad" ? {error: "Invalid duration"} : {} }
        function applyTrackFilters(values) { last = values; return true }
    }
    Component {
        id: fixture
        Scrip.TrackSearchFilters {
            host: mockHost; backend: mockBackend
            onSearchRequested: mockBackend.requests++
        }
    }
    function init() { mockHost.motion = 0; mockBackend.requests = 0; mockBackend.last = {} }
    function test_layout_data() {
        return [{tag: "phone320", size: 280, mobile: true}, {tag: "phone412", size: 372, mobile: true}, {tag: "desktop", size: 960, mobile: false}]
    }
    function test_layout(data) {
        const panel = createTemporaryObject(fixture, test, {width: data.size, mobile: data.mobile, expanded: true})
        verify(panel)
        wait(50)
        for (const key of ["genres", "tags", "durationFrom", "durationTo", "bpmFrom", "bpmTo", "createdFrom", "createdTo"]) {
            const field = findChild(panel, key)
            verify(field)
            const pos = field.mapToItem(panel, 0, 0)
            verify(pos.x >= 0)
            verify(pos.x + field.width <= panel.width + 1)
            verify(field.width >= (key.startsWith("duration") || key.startsWith("bpm") ? 50 : 100))
            verify(field.height >= (data.mobile ? 44 : 36))
        }
        verify(Math.abs(findChild(panel, "genres").width - findChild(panel, "tags").width) < 1)
        verify(panel.height > 0)
        compare(mockBackend.requests, 0)
        grabImage(panel).save("panel-" + data.tag + ".png")
    }
    function test_revealReversesAndKeepsDraft() {
        mockHost.motion = 400
        const panel = createTemporaryObject(fixture, test, {width: 372})
        const reveal = findChild(panel, "filterReveal")
        const genre = findChild(panel, "genres")
        genre.text = "House"
        compare(reveal.height, 0)
        panel.expanded = true
        tryVerify(function() { return reveal.height > 0 && reveal.opacity > 0 && reveal.opacity < 1 })
        genre.forceActiveFocus()
        verify(genre.activeFocus)
        const intermediate = reveal.height
        panel.expanded = false
        verify(Math.abs(reveal.height - intermediate) < 1)
        compare(reveal.enabled, false)
        verify(!genre.activeFocus)
        tryCompare(reveal, "height", 0)
        tryCompare(reveal, "visible", false)
        mockHost.motion = 0
        panel.expanded = true
        wait(1)
        verify(reveal.height > 0)
        compare(reveal.opacity, 1)
        compare(genre.text, "House")
        compare(mockBackend.requests, 0)
        panel.expanded = false
        wait(1)
        compare(reveal.height, 0)
    }
    function test_applyEnterReset() {
        const panel = createTemporaryObject(fixture, test, {width: 372, expanded: true})
        const genre = findChild(panel, "genres")
        genre.text = "House"
        compare(mockBackend.requests, 0)
        genre.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(mockBackend.requests, 1)
        verify(panel.summary.includes("House"))
        genre.text = "Jazz"
        compare(mockBackend.requests, 1)
        verify(panel.summary.includes("House"))
        findChild(panel, "durationFrom").text = "bad"
        verify(!panel.apply())
        compare(mockBackend.requests, 1)
        verify(panel.error.length > 0)
        panel.reset()
        compare(mockBackend.requests, 2)
        compare(panel.summary, "")
        compare(panel.error, "")
        compare(genre.text, "")
        compare(mockBackend.last.fullOnly, false)
    }
}
