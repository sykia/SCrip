import QtQuick
import QtTest
import "../../qml" as Scrip

TestCase {
    name: "Artwork"
    when: windowShown
    width: 400
    height: 400

    Component { id: artworkComponent; Scrip.Artwork { width: 32; height: 32 } }

    function waitForLoaded(item, url) {
        tryCompare(item, "displayedSource", url, 3000)
    }

    function test_sourceChangeKeepsCurrentImageUntilReplacementLoads() {
        const red = String(Qt.resolvedUrl("red.svg"))
        const blue = String(Qt.resolvedUrl("blue.svg"))
        const item = artworkComponent.createObject(this, { source: red })
        verify(item)
        waitForLoaded(item, red)
        item.source = blue
        tryCompare(item, "displayedSource", blue, 3000)
        verify(!item.isImageLoaded(red))
        item.destroy()
    }

    function test_rapidReuseAndMissingImage() {
        const item = artworkComponent.createObject(this)
        verify(item)
        for (let i = 0; i < 30; ++i)
            item.source = String(Qt.resolvedUrl(i % 2 ? "blue.svg" : "red.svg"))
        waitForLoaded(item, item.source)
        compare(item.displayedSource, item.source)
        item.source = String(Qt.resolvedUrl("missing.svg"))
        const previous = item.displayedSource
        wait(100)
        compare(item.displayedSource, previous)
        item.destroy()
    }

    function test_virtualizedListsOver2000Items() {
        const root = Qt.createQmlObject(`import QtQuick; import "../../qml" as Scrip; Item {
            width: 200; height: 200
            property alias list: listView
            property alias grid: gridView
            ListView { id: listView; objectName: "list"; anchors.fill: parent; model: 2100
                delegate: Scrip.Artwork { width: 24; height: 24; source: String(Qt.resolvedUrl(index % 2 ? "blue.svg" : "red.svg")) }
            }
            GridView { id: gridView; objectName: "grid"; anchors.fill: parent; visible: false; cellWidth: 30; cellHeight: 30; model: 2100
                delegate: Scrip.Artwork { width: 24; height: 24; source: String(Qt.resolvedUrl(index % 2 ? "blue.svg" : "red.svg")) }
            }
        }`, this)
        verify(root)
        const list = root.list
        list.positionViewAtIndex(2099, ListView.End)
        wait(50)
        list.positionViewAtIndex(0, ListView.Beginning)
        wait(50)
        const grid = root.grid
        grid.visible = true
        grid.positionViewAtIndex(2099, GridView.End)
        wait(50)
        grid.positionViewAtIndex(0, GridView.Beginning)
        wait(50)
        root.destroy()
    }
}
