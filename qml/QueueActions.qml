import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: popup
    SwipeDismiss { targetPopup: popup; enabled: popup.mobile && !popup.busy;  }
    required property var host
    property bool mobile: false
    readonly property var state: sc.uiState
    readonly property bool busy: !!state.queuePlaylistBusy
    function begin() { open(); title.forceActiveFocus() }
    parent: Overlay.overlay
    width: Math.min(440, parent.width - (mobile ? 24 : 48))
    x: (parent.width - width) / 2; y: (parent.height - height) / 2
    padding: mobile ? 20 : 24
    modal: true; focus: true
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    opacity: 0; scale: 0.985
    enter: SurfaceTransition { duration: popup.host.motion }
    exit: SurfaceTransition { duration: popup.host.motion; leaving: true }
    background: Rectangle { color: popup.host.panel; radius: 22 }
    Overlay.modal: Rectangle { color: "#99000000" }
    Connections {
        target: sc
        function onQueuePlaylistSaved() { title.clear(); popup.close() }
        function onSignedInChanged() { title.clear(); popup.close() }
    }
    component Copy: Label { color: popup.host.ink; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
    component Action: SurfaceButton {
        host: popup.host; mobile: popup.mobile; quiet: true
        Layout.fillWidth: true; implicitHeight: popup.mobile ? 48 : 40
    }
    contentItem: ColumnLayout {
        spacing: 8
        Copy { text: qsTr("Сохранить очередь"); font.pixelSize: 20; font.weight: Font.DemiBold; Layout.fillWidth: true; Layout.bottomMargin: 8 }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 12
            TextField {
                id: title; objectName: "queuePlaylistTitle"
                Layout.fillWidth: true; implicitHeight: 48; maximumLength: 255
                placeholderText: qsTr("Название плейлиста"); color: popup.host.ink
                placeholderTextColor: popup.host.muted; selectionColor: popup.host.accent
                enabled: !popup.busy
                background: Rectangle { color: popup.host.raised; radius: 10 }
                onAccepted: if (text.trim() && !popup.busy) sc.saveQueueAsPlaylist(text)
            }
            Copy { text: popup.state.queuePlaylistError || ""; visible: !!text; Layout.fillWidth: true; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                Action { text: qsTr("Отмена"); enabled: !popup.busy; onClicked: popup.close() }
                Action { text: popup.busy ? qsTr("Сохранение…") : qsTr("Сохранить"); primary: true; enabled: !!title.text.trim() && !popup.busy && sc.signedIn && sc.queueTracks.length > 0; onClicked: sc.saveQueueAsPlaylist(title.text) }
            }
        }
    }
}
