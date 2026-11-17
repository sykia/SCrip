import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml" as Scrip

TestCase {
    id: test
    name: "CollectionTools"
    when: windowShown
    visible: true
    width: 1100; height: 850
    Rectangle { anchors.fill: parent; color: mockHost.panel; z: -1 }
    QtObject {
        id: mockHost
        property color ink: dark ? "#eeeeee" : "#222222"
        property color muted: dark ? "#aaaaaa" : "#777777"
        property color raised: dark ? "#292929" : "#eeeeee"
        property color surface: dark ? "#111111" : "#fafafa"
        property color panel: dark ? "#1c1c1c" : "#ffffff"
        property color accent: "#b291ff"
        property bool dark: true
        property int motion: 0
        function foreground(color) { return "#111111" }
    }
    QtObject {
        id: mockBackend
        readonly property var collectionTracks: catalogState.tracks || []
        readonly property var playlistRows: catalogState.playlistTracks || []
        property bool signedIn: true
        property int requests: 0
        property int cancellations: 0
        readonly property var uiState: catalogState
        property var catalogState: ({tracks: [], hasMore: true})
        function displayArtist(t) { return t.metadata_artist || t.user?.username || "" }
        function cancelCollectionSearch() { cancellations++ }
        function searchWholeCollection(playlist) { requests++ }
    }
    Component { id: fixture; Scrip.CollectionTools { host: mockHost; backend: mockBackend; contextKey: "test:collection" } }
    function init() {
        mockBackend.requests = 0; mockBackend.cancellations = 0
        mockBackend.catalogState = {tracks: [{urn:"a", title:"Zulu", duration:5000}, {urn:"b", title:"Alpha", duration:2000}, {urn:"a", title:"Zulu", duration:5000}], hasMore:true}
    }
    function test_rowsAndNoImplicitRequests() {
        const panel = createTemporaryObject(fixture, test, {width:372})
        compare(panel.rows.length, 3)
        panel.sortKey = "title"
        compare(panel.rows[0].urn, "b")
        compare(panel.rows[1].urn, "a"); compare(panel.rows[2].urn, "a")
        compare(mockBackend.catalogState.tracks[0].title, "Zulu")
        panel.query = "Zulu"; compare(panel.rows.length, 2)
        panel.query = "missing"; compare(panel.rows.length, 0)
        compare(mockBackend.requests, 0)
        panel.catalogue = true; compare(panel.rows.length, 3)
        panel.contextKey = "other"; compare(panel.sortKey, "source"); compare(panel.query, "")
    }
    function test_layout_data() {
        return [{tag:"phone-light", size:280, mobile:true, dark:false}, {tag:"phone-dark", size:372, mobile:true, dark:true}, {tag:"desktop-light", size:960, mobile:false, dark:false}, {tag:"desktop-dark", size:960, mobile:false, dark:true}]
    }
    function test_layout(data) {
        mockHost.dark = data.dark
        const panel = createTemporaryObject(fixture, test, {width:data.size, mobile:data.mobile})
        panel.query = "A very long query for partial collection results"
        wait(50)
        const field = findChild(panel, "collectionQueryField"); verify(field)
        verify(field.width > 80); verify(field.height >= (data.mobile ? 44 : 34))
        field.forceActiveFocus(); keyClick(Qt.Key_A)
        compare(mockBackend.requests, 0)
        const point = field.mapToItem(panel, 0, 0)
        verify(point.x >= 0); verify(point.x + field.width <= panel.width + 1)
        grabImage(test).save("collection-tools-" + data.tag + ".png")
        const options = findChild(panel, "collectionOptionsButton")
        verify(options); mouseClick(options); wait(50)
        grabImage(test).save("collection-menu-" + data.tag + ".png")
        keyClick(Qt.Key_Escape)
    }
    function test_datesAndReverseOrderKeepMissingValuesLast() {
        mockBackend.catalogState = {tracks:[{title:"Missing"}, {title:"Newer", created_at:"2025-10-01T00:00:00Z"}, {title:"Older", created_at:"2020-01-01T00:00:00Z"}], hasMore:false}
        const panel = createTemporaryObject(fixture, test, {width:372})
        panel.sortKey = "created_at"; compare(panel.rows[0].title, "Older"); compare(panel.rows[2].title, "Missing")
        panel.descending = true; compare(panel.rows[0].title, "Newer"); compare(panel.rows[2].title, "Missing")
    }
    function test_savedSearchScopesRenameAndDelete() {
        const panel = createTemporaryObject(fixture, test, {width:372})
        panel.query = "Zulu"
        const name = findChild(panel, "savedSearchName")
        name.text = "Test search"; panel.savePreset()
        compare(panel.presets.length, 1); compare(panel.presets[0].query, "Zulu")
        const id = panel.presets[0].id
        panel.editingId = id; name.text = "Renamed"; panel.savePreset()
        compare(panel.presets.length, 1); compare(panel.presets[0].name, "Renamed")
        panel.catalogue = true; compare(panel.presets.length, 0)
        panel.catalogue = false; compare(panel.presets.length, 1)
        panel.writePreset(id, null); compare(panel.presets.length, 0)
    }
}
