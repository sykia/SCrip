import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var backend
    property bool motion: true
    property bool foreground: true
    property bool dark: false
    property color panelColor: dark ? "#1a1a1a" : "#f7f7f8"
    property color inkColor: dark ? "#f1f1f1" : "#242426"
    property color waveColor: inkColor
    readonly property var api: backend.catalogState
    readonly property bool discovery: selector.currentIndex === 1
    readonly property string mode: discovery ? "discovery" : "library"
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
        selector.currentIndex = api.waveMode === "discovery" ? 1 : 0
        backend.setWaveVisualActive(analysisActive)
    }
    Component.onDestruction: backend.setWaveVisualActive(false)

    Rectangle {
        id: switchBackground
        anchors.top: parent.top; anchors.topMargin: 18
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 28, 346); height: 42
        radius: 21
        color: Qt.alpha(root.inkColor, 0.055)
        Rectangle {
            x: selector.currentIndex * (parent.width / 2) + 3; y: 3
            width: parent.width / 2 - 6; height: parent.height - 6
            radius: height / 2
            color: root.dark ? "#424242" : "#ffffff"
            Behavior on x { NumberAnimation { duration: root.motion ? 220 : 0; easing.type: Easing.OutCubic } }
        }
        RowLayout {
            id: selector
            objectName: "waveModeSelector"
            anchors.fill: parent
            property int currentIndex: 0
            spacing: 0
            Repeater {
                model: ["Моя музыка", "Может понравиться"]
                AbstractButton {
                    InteractionFeedback {}
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1
                    text: modelData
                    checkable: true
                    checked: selector.currentIndex === index
                    Accessible.role: Accessible.PageTab
                    Accessible.name: text
                    contentItem: Text {
                        text: parent.text; color: root.inkColor
                        opacity: parent.checked ? 1 : 0.60
                        font.pixelSize: root.width < 360 ? 11 : 12
                        font.weight: parent.checked ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Item {}
                    onClicked: {
                        selector.currentIndex = index
                        if (root.api.playbackActive && root.api.waveMode && root.api.waveMode !== root.mode)
                            root.backend.startWave(root.mode)
                    }
                }
            }
        }
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
        Accessible.name: root.failed ? "Повторить загрузку волны" : root.playing ? "Приостановить волну" : "Слушать волну"
        Accessible.description: root.failed ? root.api.waveError : (root.discovery ? "Может понравиться" : "Моя музыка")
        ToolTip.visible: hovered && root.failed
        ToolTip.text: root.api.waveError || ""
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
