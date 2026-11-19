import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore

ColumnLayout {
    id: tools
    required property var host
    required property var backend
    property bool mobile: false
    property bool playlistContext: false
    readonly property var sourceRows: playlistContext ? backend.playlistRows : backend.collectionTracks
    readonly property bool hasMore: playlistContext ? !!state.playlistHasMore : !!state.hasMore
    readonly property bool busy: playlistContext ? !!state.playlistBusy : !!state.busy
    property bool catalogue: false
    property string catalogueQuery: ""
    property string catalogueKind: "tracks"
    property string contextKey: ""
    property string query: ""
    property string sortKey: "source"
    property bool descending: false
    property var filters: ({})
    signal presetApplied(string query, var filters)
    readonly property var state: backend.uiState
    readonly property bool transformed: !!query.trim() || sortKey !== "source"
    readonly property var rows: {
        const needle = (catalogue ? "" : query).trim().toLocaleLowerCase()
        let result = sourceRows.filter(t => !needle || [t.title, backend.displayArtist(t), t.user?.username].some(v => String(v || "").toLocaleLowerCase().includes(needle)))
        if (sortKey !== "source") result = result.map((track, index) => ({track, index})).sort((a, b) => {
            function value(t) { return sortKey === "artist" ? backend.displayArtist(t) : sortKey === "created_at" ? (Number.isFinite(Date.parse(t.created_at)) ? Date.parse(t.created_at) : undefined) : t[sortKey] }
            const av = value(a.track), bv = value(b.track)
            if (av === undefined || av === null || av === "") return (bv === undefined || bv === null || bv === "") ? a.index - b.index : 1
            if (bv === undefined || bv === null || bv === "") return -1
            const order = (sortKey === "duration" || sortKey === "created_at") ? Number(av) - Number(bv) : String(av).localeCompare(String(bv))
            return (descending ? -order : order) || a.index - b.index
        }).map(v => v.track)
        return result
    }
    onQueryChanged: if (backend) backend.cancelCollectionSearch()
    onContextKeyChanged: { query = ""; sortKey = "source"; descending = false; if (backend) backend.cancelCollectionSearch() }
    Settings { id: storage; category: "savedSearches"; property string entries: "[]" }
    readonly property var presets: { try { return JSON.parse(storage.entries).filter(p => p.scope === (catalogue ? "catalogue:" + catalogueKind : contextKey)) } catch (e) { return [] } }
    function writePreset(remove, entry) {
        let all = []
        try { all = JSON.parse(storage.entries) } catch (e) {}
        all = all.filter(p => p.id !== remove)
        if (entry) all.push(entry)
        storage.entries = JSON.stringify(all.slice(-100))
    }
    function savePreset() {
        if (!presetName.text.trim()) return
        if (editingId) {
            const old = presets.find(p => p.id === editingId)
            if (old) writePreset(old.id, Object.assign({}, old, {name: presetName.text.trim()}))
            presetDialog.close(); return
        }
        writePreset(editingId, {id: editingId || String(Date.now()), name: presetName.text.trim(), scope: catalogue ? "catalogue:" + catalogueKind : contextKey, query: catalogue ? catalogueQuery : query, filters: catalogue ? filters : {}, sort: sortKey, descending: descending})
        presetDialog.close()
    }
    property string editingId: ""
    onVisibleChanged: if (!visible && backend) backend.cancelCollectionSearch()
    Connections {
        target: tools.backend
        function onSignedInChanged() { tools.query = ""; tools.sortKey = "source"; tools.descending = false; tools.backend.cancelCollectionSearch() }
    }
    property bool controlsVisible: true
    property var selectedPreset: ({})
    function openOptions(anchor) {
        if (mobile) { options.x = 0; options.y = options.parent.height - options.height }
        else {
            const point = anchor.mapToItem(options.parent, 0, anchor.height + 6)
            options.x = Math.max(12, Math.min(point.x, options.parent.width - options.width - 12))
            options.y = Math.max(12, Math.min(point.y, options.parent.height - options.height - 12))
        }
        options.open()
    }
    function applyPreset(preset) {
        query = preset.query; sortKey = preset.sort || "source"; descending = !!preset.descending
        presetApplied(preset.query, preset.filters || {}); savedDialog.close()
    }
    spacing: 6
    component Copy: Text { color: tools.host.ink; font.pixelSize: 13; textFormat: Text.PlainText; elide: Text.ElideRight }
    component Action: SurfaceButton { host: tools.host; mobile: tools.mobile; quiet: true }
    component MenuAction: SurfaceButton {
        id: action
        property bool marked: false
        host: tools.host; mobile: tools.mobile; quiet: true
        Layout.fillWidth: true; implicitHeight: tools.mobile ? 48 : 42
        leftPadding: 12; rightPadding: 12
        contentItem: RowLayout {
            spacing: 12
            Copy { text: action.text; Layout.fillWidth: true; color: action.destructive ? (tools.host.dark ? "#ff8c8c" : "#bb3535") : tools.host.ink }
            Copy { text: "✓"; visible: action.marked; color: tools.host.accent }
        }
    }
    component Sheet: Popup {
        parent: Overlay.overlay
        width: Math.min(440, parent.width - 32)
        height: Math.min(parent.height - 32, contentItem.implicitHeight + topPadding + bottomPadding)
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        padding: 24; modal: true; focus: true
        opacity: 0; scale: 0.985
        enter: SurfaceTransition { duration: tools.host.motion }
        exit: SurfaceTransition { duration: tools.host.motion; leaving: true }
        background: Rectangle { color: tools.host.panel; radius: 22 }
        Overlay.modal: Rectangle { color: tools.mobile ? "#99000000" : "#28000000" }
    }
    RowLayout {
        visible: tools.controlsVisible
        Layout.fillWidth: true; spacing: 6
        TextField {
            objectName: "collectionQueryField"
            visible: !tools.catalogue
            Layout.fillWidth: true; Layout.minimumWidth: 40
            implicitHeight: tools.mobile ? 44 : 34
            leftPadding: 12; rightPadding: 12; selectByMouse: true; font.pixelSize: 13
            text: tools.query; onTextEdited: tools.query = text
            placeholderText: qsTr("Поиск в коллекции"); Accessible.name: placeholderText
            color: tools.host.ink; placeholderTextColor: tools.host.muted
            selectionColor: tools.host.accent
            background: Rectangle {
                radius: tools.mobile ? 14 : 12; color: tools.mobile ? tools.host.raised : tools.host.surface
                border.color: parent.activeFocus ? Qt.alpha(tools.host.accent, 0.65) : tools.mobile ? "transparent" : Qt.alpha(tools.host.ink, 0.16)
                Behavior on border.color { ColorAnimation { duration: tools.host.motion / 2 } }
            }
        }
        Item { visible: tools.catalogue; Layout.fillWidth: true }
        Action {
            id: optionsButton; objectName: "collectionOptionsButton"
            leftPadding: 0; rightPadding: 0
            text: qsTr("Действия с коллекцией"); Accessible.name: text
            implicitWidth: tools.mobile ? 44 : 32; implicitHeight: tools.mobile ? 44 : 32
            contentItem: Canvas {
                property color ink: tools.host.ink
                onInkChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d"); c.reset(); c.strokeStyle = ink; c.lineWidth = 1.6; c.lineCap = "round"
                    c.beginPath()
                    for (let y = -5; y <= 5; y += 5) { c.moveTo(width / 2 - 7, height / 2 + y); c.lineTo(width / 2 + 7, height / 2 + y) }
                    c.stroke()
                }
            }
            onClicked: tools.openOptions(this)
            ToolTip.visible: tools.host.showTooltips === true && (hovered); ToolTip.text: text
        }
    }
    Copy {
        Layout.fillWidth: true; color: tools.host.muted; font.pixelSize: 11
        visible: !tools.catalogue && (!!tools.query || tools.sortKey !== "source" || !!tools.state.collectionSearching || !!tools.state.collectionSearchStatus)
        text: tools.state.collectionSearching ? qsTr("Загружено ") + tools.sourceRows.length + qsTr(" · поиск по всей коллекции")
              : (tools.state.collectionSearchStatus === "error" || (tools.playlistContext ? !!tools.state.playlistError : tools.state.collectionStatus === "error" || tools.state.collectionStatus === "unavailable")) ? qsTr("Поиск прерван · показана загруженная часть")
              : tools.state.collectionSearchStatus === "limit" ? qsTr("Поиск приостановлен · можно продолжить")
              : tools.hasMore || tools.busy ? qsTr("По загруженной части · ") + tools.sourceRows.length
              : qsTr("Вся коллекция · ") + tools.sourceRows.length
    }
    Popup {
        id: options
        parent: Overlay.overlay
        width: tools.mobile ? parent.width : Math.min(320, parent.width - 24)
        height: Math.min(parent.height - 32, optionRows.implicitHeight + topPadding + bottomPadding)
        padding: tools.mobile ? 16 : 10; modal: true; focus: true
        opacity: 0; scale: 0.985
        enter: SurfaceTransition { duration: tools.host.motion }
        exit: SurfaceTransition { duration: tools.host.motion; leaving: true }
        background: Rectangle { color: tools.host.panel; radius: tools.mobile ? 28 : 22 }
        Overlay.modal: Rectangle { color: tools.mobile ? "#99000000" : "#28000000" }
        contentItem: Flickable {
            contentWidth: width; contentHeight: optionRows.implicitHeight; clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: optionRows; width: parent.width; spacing: 2
                MenuAction { text: tools.state.collectionSearching ? qsTr("Остановить поиск") : qsTr("Искать по всей коллекции"); visible: !tools.catalogue; enabled: !!tools.state.collectionSearching || (tools.hasMore && !tools.busy); onClicked: { options.close(); tools.state.collectionSearching ? tools.backend.cancelCollectionSearch() : tools.backend.searchWholeCollection(tools.playlistContext) } }
                Rectangle { visible: !tools.catalogue; Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: 6; height: 1; color: Qt.alpha(tools.host.ink, 0.08) }
                Repeater {
                    model: tools.catalogue ? [] : [{key:"source", title:qsTr("Порядок источника")}, {key:"title", title:qsTr("По названию")}, {key:"artist", title:qsTr("По исполнителю")}, {key:"duration", title:qsTr("По длительности")}, {key:"created_at", title:qsTr("По дате загрузки")}].filter(v => v.key !== "created_at" || tools.sourceRows.some(t => Number.isFinite(Date.parse(t.created_at))))
                    MenuAction { required property var modelData; text: modelData.title; marked: tools.sortKey === modelData.key; onClicked: { tools.sortKey = modelData.key; options.close() } }
                }
                MenuAction { text: qsTr("Обратный порядок"); visible: !tools.catalogue; enabled: tools.sortKey !== "source"; marked: tools.descending; onClicked: { tools.descending = !tools.descending; options.close() } }
                Rectangle { visible: !tools.catalogue; Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: 6; height: 1; color: Qt.alpha(tools.host.ink, 0.08) }
                MenuAction { text: qsTr("Сохранить поиск…"); onClicked: { options.close(); tools.editingId = ""; presetName.text = ""; presetDialog.open(); presetName.forceActiveFocus() } }
                MenuAction { text: qsTr("Сохранённые поиски"); enabled: tools.presets.length > 0; onClicked: { options.close(); savedDialog.open() } }
            }
        }
    }
    Sheet {
        id: presetDialog
        contentItem: ColumnLayout {
            spacing: 16
            Copy { Layout.fillWidth: true; text: tools.editingId ? qsTr("Название поиска") : qsTr("Сохранить поиск"); font.pixelSize: 20; font.weight: Font.DemiBold }
            TextField {
                id: presetName; objectName: "savedSearchName"
                Layout.fillWidth: true; implicitHeight: 48; maximumLength: 80
                leftPadding: 14; rightPadding: 14; font.pixelSize: 14; selectByMouse: true
                placeholderText: qsTr("Название"); color: tools.host.ink; placeholderTextColor: tools.host.muted
                selectionColor: tools.host.accent; onAccepted: tools.savePreset()
                background: Rectangle { color: tools.host.raised; radius: 10; border.color: parent.activeFocus ? Qt.alpha(tools.host.accent, 0.5) : "transparent" }
            }
            RowLayout {
                Layout.fillWidth: true
                Action { text: qsTr("Отмена"); onClicked: presetDialog.close() }
                Item { Layout.fillWidth: true }
                Action { text: qsTr("Сохранить"); primary: true; enabled: !!presetName.text.trim(); onClicked: tools.savePreset() }
            }
        }
    }
    Sheet {
        id: savedDialog; objectName: "savedSearchesDialog"
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Copy { text: qsTr("Сохранённые поиски"); font.pixelSize: 20; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Action { text: qsTr("Закрыть"); onClicked: savedDialog.close() }
            }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(320, contentHeight)
                clip: true; model: tools.presets
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width; height: tools.mobile ? 52 : 44; spacing: 4
                    MenuAction { text: modelData.name; onClicked: tools.applyPreset(modelData) }
                    Action { text: "···"; Accessible.name: qsTr("Действия с поиском"); implicitWidth: 44; onClicked: { tools.selectedPreset = modelData; presetActions.open() } }
                }
            }
        }
    }
    Sheet {
        id: presetActions; width: Math.min(320, parent.width - 32); padding: 12
        contentItem: ColumnLayout {
            spacing: 2
            MenuAction { text: qsTr("Переименовать"); onClicked: { presetActions.close(); tools.editingId = tools.selectedPreset.id; presetName.text = tools.selectedPreset.name; presetDialog.open(); presetName.forceActiveFocus() } }
            MenuAction { text: qsTr("Удалить"); destructive: true; onClicked: { tools.writePreset(tools.selectedPreset.id, null); presetActions.close() } }
        }
    }
}
