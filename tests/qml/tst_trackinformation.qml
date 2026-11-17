import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml" as Scrip

TestCase {
    id: test
    name: "TrackInformation"
    when: windowShown
    width: 412; height: 640; visible: true
    QtObject {
        id: mockHost
        property color ink: "#eeeeee"
        property color muted: "#aaaaaa"
    }
    QtObject {
        id: mockBackend
        function trackDescription(track) { return track.description || "" }
        function trackInfo(track) { return track.rows || [] }
    }
    Component {
        id: fixture
        Flickable {
            width: test.width; height: test.height
            contentHeight: info.implicitHeight; clip: true
            Scrip.TrackInformation {
                id: info; objectName: "information"
                width: parent.width - 32; x: 16
                host: mockHost; backend: mockBackend
                property int profileOpens: 0
                onProfileRequested: user => profileOpens++
                track: ({ description: "<b>Plain description</b>\n" + "Long description and a verylongunbreakablename ".repeat(80),
                    user: {urn: "soundcloud:users:uploader"}, rows: [
                        {label: "Исполнитель", value: "Credited artist"},
                        {label: "Загрузил", value: "Uploader"},
                        {label: "Теги", value: "verylongunbrokenname".repeat(30)}] })
            }
        }
    }
    function test_layoutAndSelectedData() {
        for (const width of [320, 412, 1080]) {
            test.width = width
            const view = createTemporaryObject(fixture, test)
            verify(view)
            const info = findChild(view, "information")
            verify(info.implicitHeight > 0)
            function checkWidth(item) {
                if (item.visible) verify(item.width <= info.width + 1)
                for (const child of item.children) checkWidth(child)
            }
            checkWidth(info)
            verify(view.contentHeight > view.height)
            compare(info.track.rows[0].value, "Credited artist")
            compare(info.track.rows[1].value, "Uploader")
            info.track = {description: "", rows: []}
            wait(1)
            compare(info.implicitHeight, 0)
            view.destroy()
        }
        test.width = 412
    }
}
