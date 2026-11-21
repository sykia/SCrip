import QtQuick
import QtTest
import "../../qml" as Scrip

TestCase {
    name: "WaveVisibility"
    when: windowShown
    visible: true
    width: 500; height: 400
    QtObject {
        id: waveBackend
        property var uiState: ({waveMode: "discovery", playbackActive: true, recommendationPreference: 1})
        property bool analysis: false
        property real waveLevel: 0
        property real waveBass: 0
        property real waveBeat: 0
        function setWaveVisualActive(value) { analysis = value }
    }
    Component { id: fixture; Scrip.WavePanel { width: 480; height: 350; backend: waveBackend } }
    function test_coverageFollowsSurfaceAndReverses() {
        const wave = createTemporaryObject(fixture, this)
        wave.exposure = 0.65
        compare(wave.visualOpacity, 0.65)
        verify(wave.analysisActive)
        wave.exposure = 0.2
        compare(wave.visualOpacity, 0.2)
        wave.exposure = 0.7
        compare(wave.visualOpacity, 0.7)
        wave.exposure = 0
        compare(wave.visualOpacity, 0)
        verify(!wave.animating)
        verify(!wave.analysisActive)
        const phase = wave.phase
        wait(80)
        compare(wave.phase, phase)
        wave.exposure = 0.25
        compare(wave.visualOpacity, 0.25)
        verify(wave.animating)
        compare(wave.phase, phase)
    }
    function test_foregroundFadeAndReversal() {
        const wave = createTemporaryObject(fixture, this)
        wave.foreground = false
        verify(!wave.analysisActive)
        tryVerify(() => wave.visualOpacity > 0 && wave.visualOpacity < 1)
        const opacity = wave.visualOpacity
        wave.foreground = true
        verify(Math.abs(wave.visualOpacity - opacity) < 0.02)
        tryCompare(wave, "visualOpacity", 1)
        wave.foreground = false
        tryCompare(wave, "visualOpacity", 0)
    }
    function test_reducedMotion() {
        const wave = createTemporaryObject(fixture, this)
        wave.motion = false
        wave.foreground = false
        compare(wave.visualOpacity, 0)
        wave.foreground = true
        compare(wave.visualOpacity, 1)
        verify(!wave.animating)
    }
}
