import QtQuick
import QtTest
import "../../qml" as Scrip
import "../../qml/design" as Design

TestCase {
    name: "DesktopWaveIntegration"
    when: windowShown
    visible: true
    width: 920; height: 300
    Design.Theme { id: fakeTokens }
    QtObject {
        id: fakeTransport
        property int starts: 0
        property int toggles: 0
        function startWave(mode) { ++starts }
        function togglePlayback() { ++toggles }
        function retryWave() {}
        function setWaveVisualActive(enabled) {}
    }
    Component {
        id: component
        Scrip.DesktopWaveHero { width: 900; api: ({}); backend: fakeTransport; tokens: fakeTokens; motion: false }
    }
    function test_preservesVisualAndPlaybackActions() {
        const hero = createTemporaryObject(component, this)
        verify(hero)
        const original = findChild(hero, "personalWaveCard")
        const visual = findChild(hero, "waveVisual")
        const shader = findChild(hero, "waveFluidShader")
        const button = findChild(hero, "waveHeroListen")
        verify(original && visual && shader && button)
        compare(String(shader.fragmentShader), "qrc:/shaders/wave.frag.qsb")
        compare(visual.parent.parent, original)
        verify(hero.height <= 280)
        mouseClick(button, button.width / 2, button.height / 2)
        compare(fakeTransport.starts, 1)
        hero.api = {waveMode: "library", queue: [{}], playbackActive: true}
        mouseClick(button, button.width / 2, button.height / 2)
        compare(fakeTransport.toggles, 1)
        hero.width = 580
        wait(20)
        verify(button.width >= 120)
        verify(visual.width > 0)
    }
}
