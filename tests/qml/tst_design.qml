import QtQuick
import QtTest
import "../../qml/design" as Design
import "../../qml/components" as UI

TestCase {
    name: "DesignSystem"
    when: windowShown
    visible: true
    width: 360; height: 240
    Design.Theme { id: theme }
    Rectangle { id: backdrop; width: 360; height: 240; color: "#6b8291" }
    UI.GlassSurface { id: glass; width: 240; height: 160; backdropItem: backdrop }
    function luminance(c) {
        function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b)
    }
    function contrast(a, b) {
        const x = luminance(a), y = luminance(b)
        return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05)
    }
    function test_readableThemes_data() { return [{tag: "dark", dark: true}, {tag: "light", dark: false}] }
    function test_readableThemes(data) {
        theme.dark = data.dark
        for (const surface of [theme.canvas, theme.panel, theme.elevated]) {
            verify(contrast(theme.ink, surface) >= 4.5)
            verify(contrast(theme.muted, surface) >= 4.5)
        }
        for (const accent of ["#ffffff", "#000000", "#ff0055", "#ffe800", "#238579"]) {
            theme.accent = accent
            verify(contrast(theme.accentInk, theme.selection) >= 4.5)
        }
        theme.motion = false
        compare(theme.fast, 0)
        compare(theme.transition, 0)
    }
    function test_captureFollowsPanelAndSourceGeometry() {
        glass.backdropItem = backdrop
        glass.x = 45; glass.y = 24
        tryVerify(() => Math.abs(glass.captureOrigin.x - 45) < 0.1 && Math.abs(glass.captureOrigin.y - 24) < 0.1)
        backdrop.x = 5
        tryVerify(() => Math.abs(glass.captureOrigin.x - 40) < 0.1)
        glass.x = 0; glass.y = 0; backdrop.x = 0
    }
    function test_preventRecursiveCaptureAndReleaseFallback() {
        glass.quality = 0
        compare(glass.optical, false)
        glass.quality = 1
        glass.backdropItem = glass
        compare(glass.optical, false)
        glass.backdropItem = this
        compare(glass.optical, false)
        glass.backdropItem = backdrop
        verify(glass.validBackdrop())
    }
}
