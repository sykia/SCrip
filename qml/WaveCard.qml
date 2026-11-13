import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: card
    objectName: "personalWaveCard"
    required property var api
    required property var backend
    property color accent: "#f43756"
    property bool dark: true
    property bool motion: true
    property bool inViewport: true
    property string selectedMode: "library"
    readonly property bool active: (api.waveMode || "") === selectedMode
    readonly property bool renderActive: visible && inViewport && Window.window !== null
        && Window.window.visible && Window.window.visibility !== Window.Minimized
    readonly property bool audioActive: renderActive && motion && !software && !!api.playing && !!api.waveMode
    readonly property color ink: dark ? "#ffffff" : "#231d2d"
    readonly property color secondary: dark ? "#d0c7d6" : "#504358"
    readonly property bool software: GraphicsInfo.api === GraphicsInfo.Software
    property real elapsed: 0
    property real visualEnergy: 0
    property real visualBass: 0
    property real visualBeat: 0
    implicitHeight: waveLayout.implicitHeight

    function syncAudio() {
        if (backend && typeof backend.setWaveVisualActive === "function") backend.setWaveVisualActive(audioActive)
    }
    onAudioActiveChanged: {
        syncAudio()
        if (!audioActive) { visualEnergy = 0; visualBass = 0; visualBeat = 0 }
    }
    Component.onCompleted: syncAudio()
    Component.onDestruction: {
        if (backend && typeof backend.setWaveVisualActive === "function") backend.setWaveVisualActive(false)
    }
    Timer {
        interval: 33; repeat: true
        running: card.renderActive && card.motion && !card.software
        onTriggered: {
            card.elapsed += 0.033
            card.visualEnergy = card.audioActive ? (card.backend.waveLevel || 0) : 0
            card.visualBass = card.audioActive ? (card.backend.waveBass || 0) : 0
            card.visualBeat = card.audioActive ? (card.backend.waveBeat || 0) : 0
        }
    }
    function toggleWave() {
        if (active && api.waveBusy && !(api.queue || []).length) return
        if (active && api.waveError) backend.retryWave()
        else if (active && (api.queue || []).length) backend.togglePlayback()
        else backend.startWave(selectedMode)
    }
    Column {
        id: waveLayout
        width: parent.width; spacing: 12
        RowLayout {
            width: parent.width
            Label { text: "НА ТВОЕЙ ЧАСТОТЕ"; color: card.secondary; font.pixelSize: 10; font.letterSpacing: 2.5; font.weight: Font.Medium }
            Item { Layout.fillWidth: true }
            Label { text: "BETA  /  ∞"; color: card.secondary; font.pixelSize: 10; font.letterSpacing: 1 }
        }
        Label {
            objectName: "waveHeading"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "Своя волна"; color: card.ink
            font.pixelSize: card.width < 500 ? 32 : 38; font.weight: Font.Bold; font.letterSpacing: -1.2
        }
        Item {
            id: visual
            objectName: "waveVisual"
            width: parent.width; height: card.width < 500 ? 260 : 240
            ShaderEffect {
                id: fluid
                objectName: "waveFluidShader"
                anchors.fill: parent
                visible: !card.software
                readonly property vector2d resolution: Qt.vector2d(width, height)
                readonly property color themeColor: card.accent
                readonly property real time: card.elapsed
                readonly property real energy: card.visualEnergy
                readonly property real bass: card.visualBass
                readonly property real beat: card.visualBeat
                readonly property real darkMode: card.dark ? 1 : 0
                fragmentShader: "qrc:/shaders/wave.frag.qsb"
                // A small render target is upscaled: softness is intentional, no blur passes.
                layer.enabled: true
                layer.smooth: true
                layer.textureSize: Qt.size(Math.max(1, Math.min(720, width)), Math.max(1, Math.round(height * Math.min(1, 720 / Math.max(1, width)))))
            }
            // Qt's software renderer cannot execute ShaderEffect.
            Item {
                anchors.fill: parent; visible: card.software
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        width: 180 - index * 30; height: width; radius: width / 2
                        x: (visual.width - width) / 2 + (index - 1) * 24
                        y: (visual.height - height) / 2 + (index - 1) * 10
                        color: Qt.alpha(card.accent, 0.10 + index * 0.08)
                    }
                }
            }
            MouseArea {
                objectName: "waveInteractionArea"
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                activeFocusOnTab: true
                readonly property bool loading: card.active && !!card.api.waveBusy && !(card.api.queue || []).length
                enabled: !loading
                Accessible.role: Accessible.Button
                Accessible.name: loading ? "Загрузка волны" : card.active && card.api.playbackActive ? "Приостановить волну" : "Слушать волну"
                Accessible.onPressAction: card.toggleWave()
                onClicked: card.toggleWave()
                Keys.onSpacePressed: event => { card.toggleWave(); event.accepted = true }
                Keys.onReturnPressed: event => { card.toggleWave(); event.accepted = true }
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4
                    visible: parent.activeFocus
                    color: "transparent"; radius: 16
                    border.color: Qt.alpha(card.accent, 0.4)
                }
            }
        }
        Column {
            id: waveControls
            objectName: "waveControls"
            width: parent.width; spacing: 12
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - 32, 282); height: 44; radius: 22
                color: card.dark ? "#a018151e" : "#b2ffffff"
                border.color: Qt.alpha(card.accent, 0.18)
                Row {
                    anchors.fill: parent; anchors.margins: 4
                    Repeater {
                        model: [{key: "library", title: "Моя музыка"}, {key: "discovery", title: "Открытия"}]
                        Button {
                            id: modeButton
                            required property var modelData
                            width: parent.width / 2; height: parent.height
                            checkable: true; checked: card.selectedMode === modelData.key
                            text: modelData.title
                            background: Rectangle {
                                radius: 18
                                color: modeButton.checked ? (card.dark ? Qt.tint("#39313f", Qt.alpha(card.accent, 0.16)) : "#ffffff") : "transparent"
                                border.color: modeButton.checked ? Qt.alpha(card.accent, 0.28) : "transparent"
                            }
                            contentItem: Label { text: modeButton.text; color: card.ink; font.pixelSize: 12; font.weight: modeButton.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            onClicked: { card.selectedMode = modelData.key; if (card.api.waveMode && !card.active) card.backend.startWave(card.selectedMode) }
                        }
                    }
                }
            }
            Label {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: card.api.waveError || (card.api.waveBusy ? "Ищем продолжение…" : card.active && card.api.playbackActive ? "Нажми на волну, чтобы поставить на паузу" : card.active ? "Нажми на волну, чтобы продолжить" : "Нажми на волну, чтобы слушать")
                color: card.secondary; font.pixelSize: 11
            }
        }
    }
}
