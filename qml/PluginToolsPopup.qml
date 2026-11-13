import QtQuick
import "design" as Design
import "components" as UI
import QtQuick.Controls

Popup {
    id: root
    property Item backdropItem: null
    property int glassQuality: 1
    property bool dark: false
    Design.Theme { id: theme; dark: root.dark }

    property color ink: "#242426"
    property color muted: "#858589"
    property var manager: null
    property string message: ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, parent.width - 24)
    height: Math.min(600, parent.height - 32)
    background: UI.GlassSurface { dark: root.dark; quality: root.glassQuality; backdropItem: root.backdropItem; color: theme.elevated }
    modal: true
    palette.window: theme.panel
    palette.windowText: root.ink
    palette.text: root.ink
    contentItem: ScrollView {
        clip: true; contentWidth: availableWidth
        Column {
            width: parent.width; spacing: 12
            Label { text: "Дополнения плеера"; font.bold: true }
            PluginPanels { dark: root.dark; ink: root.ink; muted: root.muted; width: parent.width; manager: root.manager; slotName: "player"; onReport: error => root.message = error }
            Label { width: parent.width; text: root.message; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text }
            PluginAction { text: "Закрыть"; onClicked: root.close() }
        }
    }
}
