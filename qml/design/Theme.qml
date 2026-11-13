import QtQuick

QtObject {
    property bool dark: true
    property color accent: "#f43756"
    property bool motion: true
    readonly property color canvas: dark ? "#101114" : "#f7f8fa"
    readonly property color panel: dark ? "#17191d" : "#eef0f4"
    readonly property color elevated: dark ? "#202228" : "#ffffff"
    readonly property color hover: dark ? "#2b2e36" : "#e5e8ee"
    readonly property color pressed: dark ? "#363a44" : "#d9dde5"
    readonly property color ink: dark ? "#f4f5f7" : "#191b20"
    readonly property color muted: dark ? "#a4a8b2" : "#626773"
    readonly property color errorSurface: dark ? "#322027" : "#fff0f2"
    readonly property color errorInk: dark ? "#ff9bab" : "#a2213d"
    readonly property color line: dark ? "#333740" : "#d6dae2"
    function luminance(c) {
        function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126*linear(c.r)+0.7152*linear(c.g)+0.0722*linear(c.b)
    }
    readonly property color accentInk: {
        let c = accent
        const background = luminance(selection)
        for (let i=0; i<20; ++i) {
            const l = luminance(c)
            if ((Math.max(l, background)+0.05)/(Math.min(l, background)+0.05) >= 4.5) return c
            c = Qt.tint(c, dark ? Qt.rgba(1,1,1,0.12) : Qt.rgba(0,0,0,0.12))
        }
        return ink
    }
    readonly property color selection: Qt.tint(panel, Qt.alpha(accent, dark ? 0.19 : 0.12))
    readonly property int smallRadius: 8
    readonly property int controlRadius: 12
    readonly property int panelRadius: 20
    readonly property int spacing: 8
    readonly property int bodySize: 14
    readonly property int captionSize: 12
    readonly property int titleSize: 32
    readonly property int fast: motion ? 140 : 0
    readonly property int transition: motion ? 220 : 0
}
