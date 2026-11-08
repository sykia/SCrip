import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Column {
    id: root
    property var manager: null
    property string slotName: "home"
    property color ink: "#242426"
    property color muted: "#858589"
    signal report(string message)
    spacing: 12
    Repeater {
        model: root.manager ? root.manager.panels.filter(p => p.page === root.slotName) : []
        Rectangle {
            id: card
            required property var modelData
            width: root.width
            height: content.implicitHeight + 24
            color: "#f5f2f5"
            radius: 14
            Column {
                id: content
                x: 12; y: 12; width: parent.width - 24; spacing: 8
                Label { width: parent.width; text: card.modelData.label || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.bold: true; color: root.ink }
                Label { width: parent.width; text: card.modelData.text || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text; color: root.muted }
                Label { width: parent.width; text: card.modelData.timerText || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text; color: root.ink }
                Repeater {
                    model: card.modelData.displayItems || []
                    Row {
                        id: stateItem
                        required property var modelData
                        spacing: 8
                        Label {
                            text: stateItem.modelData.label + ": " + (stateItem.modelData.available ? (stateItem.modelData.valueType === "boolean" ? (stateItem.modelData.value ? "Включено" : "Выключено") : String(stateItem.modelData.value)) : "—")
                            textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.ink
                        }
                        Switch { visible: stateItem.modelData.type === "toggle" && stateItem.modelData.available; checked: stateItem.modelData.value === true; enabled: false }
                    }
                }
                Flow {
                    width: parent.width; spacing: 8
                    Repeater {
                        model: card.modelData.buttons || []
                        PluginAction {
                            required property var modelData
                            required property int index
                            text: modelData.label
                            width: Math.min(implicitWidth, content.width)
                            Accessible.name: text
                            onClicked: root.report(root.manager.invokeButton(card.modelData.pluginId, card.modelData.contributionId, index))
                        }
                    }
                }
            }
        }
    }
}
