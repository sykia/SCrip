import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var backend
    property bool motion: true
    property bool showTooltips: false
    property bool foreground: true
    property bool dark: false
    property color panelColor: dark ? "#1a1a1a" : "#f7f7f8"
    property color inkColor: dark ? "#f1f1f1" : "#242426"
    property color waveColor: inkColor
    readonly property var api: backend.uiState
    readonly property bool discovery: true
    readonly property string mode: "discovery"
    readonly property bool selected: api.waveMode === mode
    readonly property bool playing: selected && !!api.playbackActive
    readonly property bool loading: selected && !!api.waveBusy && !api.current?.urn
    readonly property bool failed: selected && !!api.waveError
    readonly property bool animating: visible && foreground && motion
    readonly property bool analysisActive: animating && playing
    implicitHeight: width < 540 ? 330 : 350
    radius: 24
    color: panelColor
    onAnalysisActiveChanged: backend.setWaveVisualActive(analysisActive)
    Component.onCompleted: {
        backend.setWaveVisualActive(analysisActive)
    }
    Component.onDestruction: backend.setWaveVisualActive(false)

    PlaybackQuality {
        id: switchBackground
        objectName: "wavePreferenceSelector"
        anchors.top: parent.top; anchors.topMargin: 18
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 28, 480)
        height: stacked ? 152 : 52
        stacked: width < 250
        segments: [qsTr("Больше знакомого"), qsTr("Баланс"), qsTr("Больше открытий")]
        Accessible.name: qsTr("Подбор музыки")
        value: root.api.recommendationPreference === undefined ? 1 : root.api.recommendationPreference
        ink: root.inkColor; surface: Qt.alpha(root.inkColor, 0.055); accent: root.waveColor
        selectedInk: root.waveColor.hslLightness > 0.6 ? "#242426" : "white"
        motion: root.motion ? 150 : 0
        onEdited: value => root.backend.chooseWavePreference(value)
        Accessible.description: qsTr("Изменить подбор и включить другой трек")
    }
    AbstractButton {
        InteractionFeedback {}
        id: control
        objectName: "wavePlayButton"
        anchors.top: switchBackground.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.right: parent.right
        hoverEnabled: true
        enabled: !root.loading
        Accessible.name: root.failed ? qsTr("Повторить загрузку волны") : root.playing ? qsTr("Приостановить волну") : qsTr("Слушать волну")
        Accessible.description: root.failed ? root.api.waveError : root.discovery && root.api.current?.recommendationSource ? qsTr("Похоже на «") + root.api.current.recommendationSource + "» · SoundCloud" : (root.discovery ? qsTr("Больше открытий") : qsTr("Больше знакомого"))
        ToolTip.visible: root.showTooltips === true && (hovered && (root.failed || (root.discovery && !!root.api.current?.recommendationSource)))
        ToolTip.text: Accessible.description
        background: Item {}
        onClicked: {
            if (root.failed) root.backend.retryWave()
            else root.backend.startWave(root.mode)
        }
        contentItem: Item {
            Loader {
                id: visualLoader
                objectName: "waveShaderLoader"
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height * 1.7)
                height: parent.height
                active: root.visible && root.foreground && GraphicsInfo.api !== GraphicsInfo.Software
                sourceComponent: ShaderEffect {
                    id: visual
                    objectName: "waveShader"
                    property real phase: 0
                    property real level: root.animating && root.playing ? root.backend.waveLevel : 0
                    property real bass: root.animating && root.playing ? root.backend.waveBass : 0
                    property real beat: root.animating && root.playing ? root.backend.waveBeat : 0
                    property real discovery: root.discovery ? 1 : 0
                    property real aspect: width / Math.max(1, height)
                    property color inkColor: root.waveColor
                    property real engaged: root.playing ? 1 : control.hovered ? 0.35 : 0
                    fragmentShader: "qrc:/shaders/wave.frag.qsb"
                    NumberAnimation on phase { from: 0; to: Math.PI * 40; duration: 180000; loops: Animation.Infinite; running: root.animating }
                    Behavior on discovery { NumberAnimation { duration: root.motion ? 600 : 0; easing.type: Easing.InOutCubic } }
                    Behavior on engaged { NumberAnimation { duration: root.motion ? 350 : 0 } }
                }
            }
            Repeater {
                model: root.visible && root.foreground && GraphicsInfo.api === GraphicsInfo.Software ? 5 : 0
                Rectangle {
                    required property int index
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) * (0.58 + index * 0.035)
                    height: width; radius: width / 2
                    color: "transparent"; border.width: 1
                    border.color: Qt.alpha(root.waveColor, 0.4 - index * 0.06)
                }
            }
            Item {
                anchors.centerIn: parent
                width: 32; height: 32
                scale: control.down ? 0.85 : 1
                opacity: control.hovered || control.activeFocus ? 0.9 : root.playing ? 0.45 : 0.72
                Behavior on scale { NumberAnimation { duration: root.motion ? 120 : 0 } }
                Behavior on opacity { NumberAnimation { duration: root.motion ? 180 : 0 } }
                Canvas {
                    anchors.fill: parent
                    visible: !root.playing && !root.loading
                    property color ink: root.inkColor
                    property bool retry: root.failed
                    onInkChanged: requestPaint()
                    onRetryChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset(); ctx.strokeStyle = ink; ctx.fillStyle = ink
                        if (retry) {
                            ctx.lineWidth = 2; ctx.beginPath(); ctx.arc(16, 16, 9, 0.3, 5.7); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(25, 8); ctx.lineTo(25, 15); ctx.lineTo(18, 12); ctx.fill()
                        } else {
                            ctx.beginPath(); ctx.moveTo(12, 7); ctx.lineTo(26, 16); ctx.lineTo(12, 25); ctx.closePath(); ctx.fill()
                        }
                    }
                }
                Row {
                    anchors.centerIn: parent; spacing: 5
                    visible: root.playing && !root.loading
                    Repeater { model: 2; Rectangle { width: 4; height: 18; radius: 2; color: root.inkColor } }
                }
                BusyIndicator { anchors.fill: parent; running: root.loading; visible: running; palette.dark: root.inkColor }
            }
            Rectangle {
                anchors.fill: parent; anchors.margins: 8
                radius: 18; color: "transparent"
                border.width: control.activeFocus ? 1 : 0
                border.color: Qt.alpha(root.inkColor, 0.35)
            }
        }
    }
}
