import QtQuick
import QtQuick.Window
import QtQml.Models

// Capture only an explicitly supplied sibling/ancestor-free backdrop. Foreground
// children are drawn after the optical pass and never enter the capture.
Rectangle {
    id: glass
    property Item backdropItem: null
    property bool dark: true
    // 0: opaque / software, 1: balanced, 2: high quality.
    property int quality: 1
    property real density: dark ? 0.68 : 0.72
    property real refraction: 1.5
    function validBackdrop() {
        if (!backdropItem) return false
        let item = glass
        while (item) {
            if (item === backdropItem) return false
            item = item.parent
        }
        return true
    }
    readonly property bool optical: quality > 0 && validBackdrop()
        && visible && width > 0 && height > 0 && GraphicsInfo.api !== GraphicsInfo.Software
    property point captureOrigin: Qt.point(0, 0)
    function updateCaptureMapping() {
        if (backdropItem && visible) captureOrigin = mapToItem(backdropItem, 0, 0)
    }
    function scheduleMapping() { Qt.callLater(updateCaptureMapping) }
    readonly property var mappingItems: {
        const items = []
        function appendChain(item) {
            while (item) {
                if (items.indexOf(item) === -1) items.push(item)
                item = item.parent
            }
        }
        appendChain(parent)
        appendChain(backdropItem)
        return items
    }
    onMappingItemsChanged: scheduleMapping()
    onXChanged: scheduleMapping()
    onYChanged: scheduleMapping()
    onWidthChanged: scheduleMapping()
    onHeightChanged: scheduleMapping()
    onOpticalChanged: scheduleMapping()
    onVisibleChanged: scheduleMapping()
    Instantiator {
        model: glass.mappingItems
        delegate: Connections {
            required property var modelData
            target: modelData
            function onXChanged() { glass.scheduleMapping() }
            function onYChanged() { glass.scheduleMapping() }
            function onWidthChanged() { glass.scheduleMapping() }
            function onHeightChanged() { glass.scheduleMapping() }
            function onScaleChanged() { glass.scheduleMapping() }
            function onRotationChanged() { glass.scheduleMapping() }
        }
    }
    color: dark ? "#202228" : "#ffffff"
    radius: 20
    border.color: dark ? "#403f4752" : "#507d8696"
    border.width: 1
    ShaderEffectSource {
        id: capture
        visible: false
        sourceItem: glass.optical ? glass.backdropItem : null
        sourceRect: Qt.rect(glass.captureOrigin.x, glass.captureOrigin.y, glass.width, glass.height)
        textureSize: Qt.size(Math.max(1, Math.ceil(glass.width * (glass.quality === 2 ? 0.75 : 0.4))),
                             Math.max(1, Math.ceil(glass.height * (glass.quality === 2 ? 0.75 : 0.4))))
        live: glass.optical && glass.Window.window !== null && glass.Window.window.visible && glass.Window.window.visibility !== Window.Minimized
        hideSource: false
        recursive: false
    }
    ShaderEffect {
        anchors.fill: parent
        visible: glass.optical
        property var backdrop: capture
        property vector2d resolution: Qt.vector2d(width, height)
        property color tint: glass.color
        property real cornerRadius: glass.radius
        property real density: glass.density
        property real refraction: glass.refraction / Math.max(1, glass.Window.window ? glass.Window.window.devicePixelRatio : 1)
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
    }
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: "transparent"
        border.width: 1
        border.color: glass.dark ? "#18ffffff" : "#b0ffffff"
    }
}
