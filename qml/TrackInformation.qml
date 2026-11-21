import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var track
    required property var host
    property var backend: sc
    signal profileRequested(var user)
    spacing: 14
    MetadataLabel {
        Layout.fillWidth: true
        text: root.backend.trackDescription(root.track)
        visible: text.length > 0
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.host.muted; font.pixelSize: 13; lineHeight: 1.4
    }
    Repeater {
        model: root.backend.trackInfo(root.track)
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true; spacing: 4
            MetadataLabel { text: modelData.label; color: root.host.muted; font.pixelSize: 11; textFormat: Text.PlainText }
            AbstractButton {
                id: field
                readonly property bool profileLink: modelData.label === qsTr("Загрузил") && !!root.track.user?.urn
                Layout.fillWidth: true; padding: 0
                focusPolicy: profileLink ? Qt.StrongFocus : Qt.NoFocus
                Accessible.name: modelData.value
                Accessible.role: profileLink ? Accessible.Link : Accessible.StaticText
                onClicked: if (profileLink) root.profileRequested(root.track.user)
                contentItem: MetadataLabel {
                    text: modelData.value; textFormat: Text.PlainText
                    color: root.host.ink; font.pixelSize: 13; wrapMode: Text.Wrap
                }
                background: Rectangle { color: "transparent"; border.width: field.visualFocus ? 1 : 0; border.color: root.host.muted; radius: 3 }
            }
        }
    }
}
