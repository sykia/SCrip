import QtQuick
import QtQuick.Controls

Popup {
    id: root
    property bool dark: false

    property color ink: "#242426"
    property color muted: "#858589"
    property var manager: null
    property string message: ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, parent.width - 24)
    height: Math.min(600, parent.height - 32)
    modal: true
    palette.window: (root.dark ? "#1c1c1c" : "#fafafa")
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
