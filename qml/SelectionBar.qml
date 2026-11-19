import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: bar
    required property var host
    required property var selection
    property var backend: sc
    property bool mobile: false
    property bool showEntry: true
    property int serial: 0
    readonly property bool writeBusy: !!backend.uiState.savingTrack || !!backend.uiState.queuePlaylistBusy || !!backend.uiState.playlistEditBusy
    spacing: 4
    implicitHeight: controls.implicitHeight + (status.visible ? status.implicitHeight + spacing : 0)
    function back() {
        if (picker.visible) { if (!selection.busy) picker.close(); return true }
        if (selection.active) { if (!selection.busy) selection.reset(); return true }
        return false
    }
    Connections {
        target: bar.backend
        function onSelectionSaved(token, success, error) {
            if (bar.selection.pending !== token) return
            bar.selection.finish(token, success, error)
            if (success) picker.close()
        }
        function onSignedInChanged() { bar.selection.reset(); picker.close() }
    }
    Connections {
        target: bar.selection
        function onContextChanged() { picker.close() }
        function onActiveChanged() { if (!bar.selection.active) picker.close() }
    }
    Flow {
        id: controls
        objectName: "selectionControls"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: 4
        SurfaceButton {
            host: bar.host; mobile: bar.mobile; quiet: true
            visible: bar.showEntry && !bar.selection.active; text: bar.mobile ? qsTr("Выбор треков ⋯") : qsTr("Выбрать треки")
            onClicked: bar.mobile ? selectionMenu.open() : bar.selection.active = true
            Menu { id: selectionMenu; MenuItem { text: qsTr("Выбрать треки"); onTriggered: bar.selection.active = true } }
        }
        Label { visible: bar.selection.active; text: qsTr("Выбрано: ") + bar.selection.count; color: bar.host.muted; font.pixelSize: 12; height: bar.mobile ? 44 : 36; verticalAlignment: Text.AlignVCenter; padding: 6 }
        SurfaceButton { host: bar.host; mobile: true; quiet: true; visible: bar.selection.active && bar.mobile; text: "×"; Accessible.name: qsTr("Выйти из режима выбора"); enabled: !bar.selection.busy; onClicked: bar.selection.reset() }
        SurfaceButton { host: bar.host; mobile: bar.mobile; quiet: true; visible: bar.selection.active; text: qsTr("В очередь"); enabled: bar.selection.count > 0 && !bar.selection.busy; onClicked: { const error = bar.backend.enqueueTracks(bar.selection.tracks()); if (error) bar.selection.message = error; else { bar.selection.reset(); bar.selection.message = qsTr("Добавлено в очередь") } } }
        SurfaceButton { host: bar.host; mobile: bar.mobile; quiet: true; visible: bar.selection.active; text: qsTr("В плейлист"); enabled: bar.selection.count > 0 && !bar.selection.busy && !bar.writeBusy && bar.backend.signedIn; onClicked: { title.clear(); picker.open(); bar.backend.listDestinations() } }
        SurfaceButton { host: bar.host; mobile: bar.mobile; quiet: true; visible: bar.selection.active; text: qsTr("Все загруженные"); Accessible.name: qsTr("Выбрать все загруженные записи"); enabled: !bar.selection.busy; onClicked: bar.selection.allLoaded() }
        SurfaceButton { host: bar.host; mobile: bar.mobile; quiet: true; visible: bar.selection.active && !bar.mobile; text: "×"; Accessible.name: qsTr("Выйти из режима выбора"); enabled: !bar.selection.busy; onClicked: bar.selection.reset() }
    }
    Label { id: status; Layout.fillWidth: true; visible: !!text; text: bar.selection.message; color: bar.host.muted; font.pixelSize: 12; wrapMode: Text.Wrap; textFormat: Text.PlainText; Accessible.role: Accessible.StaticText }
    Timer { interval: 2500; running: !bar.selection.active && !!bar.selection.message; onTriggered: bar.selection.message = "" }
    Popup {
        id: picker
        parent: Overlay.overlay
        width: Math.min(420, parent.width - 24); x: (parent.width - width) / 2; y: (parent.height - height) / 2
        padding: 16; modal: true; focus: true
        closePolicy: bar.selection.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: bar.host.panel; radius: 16 }
        function save(urn, name) {
            if (bar.selection.busy || bar.writeBusy) return
            // Object identity separates simultaneous surfaces; revisions reject old contexts.
            const token = String(bar) + ":" + bar.selection.revision + ":" + (++bar.serial)
            bar.selection.begin(token)
            bar.backend.saveTracksToPlaylist(bar.selection.tracks(), urn, name, token)
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label { text: qsTr("Добавить в плейлист"); color: bar.host.ink; font.pixelSize: 18 }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(220, count * (bar.mobile ? 48 : 40)); clip: true
                model: bar.backend.uiState.destinations || []
                delegate: SurfaceButton { required property var modelData; width: ListView.view.width; host: bar.host; mobile: bar.mobile; text: modelData.title; enabled: !bar.selection.busy && !bar.writeBusy; onClicked: picker.save(modelData.urn, "") }
            }
            SurfaceButton { host: bar.host; mobile: bar.mobile; visible: !!bar.backend.uiState.destinationNext; text: qsTr("Ещё плейлисты"); enabled: !bar.backend.uiState.loadingDestinations; onClicked: bar.backend.listDestinations(bar.backend.uiState.destinationNext) }
            TextField { id: title; Layout.fillWidth: true; maximumLength: 255; placeholderText: qsTr("Название нового плейлиста"); Accessible.name: placeholderText; color: bar.host.ink; background: Rectangle { color: bar.host.raised; radius: 8 } enabled: !bar.selection.busy }
            Label { Layout.fillWidth: true; text: bar.selection.message || bar.backend.uiState.error || ""; visible: !!text; color: bar.host.muted; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            SurfaceButton { host: bar.host; mobile: bar.mobile; text: bar.selection.busy ? qsTr("Сохранение…") : qsTr("Создать и добавить"); enabled: !!title.text.trim() && !bar.selection.busy && !bar.writeBusy; onClicked: picker.save("", title.text) }
            SurfaceButton { host: bar.host; mobile: bar.mobile; text: qsTr("Закрыть"); enabled: !bar.selection.busy; onClicked: picker.close() }
        }
    }
}
