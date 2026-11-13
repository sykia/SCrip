import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Scrip

TestCase {
    name: "PersonalWave"
    when: windowShown
    visible: true
    width: 640; height: 540
    QtObject {
        id: fakeBackend
        property int starts: 0
        property int toggles: 0
        property int retries: 0
        property string mode: ""
        function startWave(value) { ++starts; mode = value }
        function togglePlayback() { ++toggles }
        function retryWave() { ++retries }
        function setWaveVisualActive(enabled) {}
    }
    Component {
        id: waveComponent
        Scrip.WaveCard { width: 640; api: ({}); backend: fakeBackend; motion: false }
    }
    Component {
        id: windowComponent
        Window {
            width: 640; height: 540; visible: true
            property alias wave: windowWave
            Scrip.WaveCard { id: windowWave; width: parent.width; api: ({}); backend: fakeBackend; motion: false }
        }
    }
    Component { id: otherWindowComponent; Window { width: 160; height: 120; visible: true } }
    function init() { fakeBackend.starts = 0; fakeBackend.toggles = 0; fakeBackend.retries = 0 }
    function test_clickVisualStartsPausesResumesAndRetries() {
        const card = createTemporaryObject(waveComponent, this)
        verify(card)
        const area = findChild(card, "waveInteractionArea")
        mouseClick(area, area.width / 2, area.height / 2)
        compare(fakeBackend.starts, 1)
        compare(fakeBackend.mode, "library")
        card.api = {waveMode: "library", queue: [{}], playbackActive: true}
        mouseClick(area, area.width / 2, area.height / 2)
        compare(fakeBackend.toggles, 1)
        card.api = {waveMode: "library", queue: [{}], playbackActive: false}
        mouseClick(area, area.width / 2, area.height / 2)
        compare(fakeBackend.toggles, 2)
        area.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(fakeBackend.toggles, 3)
        card.api = {waveMode: "library", waveError: "retry"}
        mouseClick(area, area.width / 2, area.height / 2)
        compare(fakeBackend.retries, 1)
        card.api = {waveMode: "library", waveBusy: true, queue: []}
        mouseClick(area, area.width / 2, area.height / 2)
        compare(fakeBackend.starts, 1)
        compare(fakeBackend.toggles, 3)
    }
    function test_controlsStayOutsideShader_data() { return [{tag: "desktop", width: 640}, {tag: "mobile", width: 360}] }
    function test_controlsStayOutsideShader(data) {
        const card = createTemporaryObject(waveComponent, this, {width: data.width})
        verify(card)
        const heading = findChild(card, "waveHeading")
        const visual = findChild(card, "waveVisual")
        const controls = findChild(card, "waveControls")
        verify(heading.y + heading.height <= visual.y)
        verify(visual.y + visual.height <= controls.y)
        verify(controls.y + controls.height <= card.height)
    }
    function test_minimizingStopsVisualAndRestoringResumesIt() {
        const win = createTemporaryObject(windowComponent, this)
        verify(win)
        tryCompare(win.wave, "renderActive", true)
        const other = createTemporaryObject(otherWindowComponent, this)
        verify(other)
        other.requestActivate()
        // The offscreen platform may ignore activation; either way rendering continues.
        compare(win.wave.renderActive, true)
        win.visibility = Window.Minimized
        tryCompare(win.wave, "renderActive", false)
        win.visibility = Window.Windowed
        tryCompare(win.wave, "renderActive", true)
        win.wave.inViewport = false
        compare(win.wave.renderActive, false)
        win.wave.inViewport = true
        tryCompare(win.wave, "renderActive", true)
        win.visible = false
        compare(win.wave.renderActive, false)
    }
}
