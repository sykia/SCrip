import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: editor
    required property var host
    property bool mobile: false
    property var playlist: ({})
    property bool initialized: false
    property bool tracksChanged: false
    property int selectedTrackIndex: -1
    readonly property var state: sc.uiState
    readonly property var draft: state.playlistDraft || ({})
    readonly property bool busy: !!state.playlistEditBusy
    readonly property bool dirty: initialized && (tracksChanged || titleField.text !== (playlist.title || "") || description.text !== (playlist.description || "") || privacy.checked !== (playlist.sharing === "private"))
    signal saved(var item)
    signal deleted(string urn)
    function begin(item) {
        playlist = item; initialized = false; tracksChanged = false
        titleField.text = ""; description.text = ""; privacy.checked = false
        sc.editPlaylist(item); open()
    }
    function dismiss() {
        if (list.dragFrom >= 0) { list.cancelDrag(); return }
        if (rowActions.visible) { rowActions.close(); return }
        if (options.visible) { options.close(); return }
        if (confirmDelete.visible) { confirmDelete.close(); return }
        if (discard.visible) { discard.close(); return }
        if (!busy || !initialized) { if (dirty) discard.open(); else close() }
    }
    parent: Overlay.overlay
    width: mobile ? parent.width : Math.min(700, parent.width - 64)
    height: Math.min(parent.height - (mobile ? 20 : 64), 800)
    x: (parent.width - width) / 2; y: mobile ? parent.height - height : (parent.height - height) / 2
    padding: mobile ? 20 : 28
    bottomPadding: padding + (mobile ? host.safeAreaBottom(host) : 0)
    opacity: 0; scale: 0.985
    modal: true; focus: true; closePolicy: Popup.NoAutoClose
    onClosed: { list.cancelDrag(); sc.cancelPlaylistEdit() }
    background: Rectangle { color: editor.host.panel; radius: 24; border.color: Qt.alpha(editor.host.ink, 0.08); border.width: editor.mobile ? 0 : 1 }
    Overlay.modal: Rectangle { color: "#99000000" }
    enter: SurfaceTransition { duration: editor.host.motion }
    exit: SurfaceTransition { duration: editor.host.motion; leaving: true }
    Connections {
        target: sc
        function onCatalogChanged() {
            if (!editor.visible || editor.initialized || !editor.draft.urn) return
            editor.playlist = editor.draft
            titleField.text = editor.draft.title || ""
            description.text = editor.draft.description || ""
            privacy.checked = editor.draft.sharing === "private"
            editor.initialized = true
        }
        function onPlaylistUpdated(item) { if (editor.visible) { editor.tracksChanged = false; editor.initialized = false; editor.close(); editor.saved(item) } }
        function onPlaylistDeleted(urn) { if (editor.visible) { editor.initialized = false; editor.close(); editor.deleted(urn) } }
        function onSignedInChanged() { if (!sc.signedIn && editor.visible) { editor.initialized = false; editor.close() } }
    }
    component Copy: Label { color: editor.host.ink; textFormat: Text.PlainText; elide: Text.ElideRight }
    component QuietButton: SurfaceButton { host: editor.host; mobile: editor.mobile; quiet: true; implicitWidth: 40; implicitHeight: 40; leftPadding: 8; rightPadding: 8 }
    // The lifted row and the stationary rows share the same geometry.
    component TrackContent: RowLayout {
        property var track: ({})
        property int number: 0
        property bool floating: false
        spacing: 12
        Artwork {
            Layout.preferredWidth: 40; Layout.preferredHeight: 40; radius: 8
            source: editor.host.cover(parent.track)
            Copy { anchors.centerIn: parent; text: "♪"; font.pixelSize: 18; color: editor.host.muted; visible: !parent.source }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 4
            Copy { Layout.fillWidth: true; text: parent.parent.track.title || "Недоступный трек"; font.pixelSize: 13; font.weight: Font.Medium }
            Copy { Layout.fillWidth: true; text: sc.displayArtist(parent.parent.track) || "SoundCloud"; color: editor.host.muted; font.pixelSize: 11 }
        }
    }
    contentItem: ColumnLayout {
        focus: true
        Keys.onEscapePressed: function(event) { editor.dismiss(); event.accepted = true }
        spacing: 16
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Copy { Layout.fillWidth: true; text: "Редактирование плейлиста"; color: editor.host.muted; font.pixelSize: 12 }
            QuietButton { text: "⋯"; Accessible.name: "Действия с плейлистом"; enabled: editor.initialized && !editor.busy; onClicked: options.open() }
            QuietButton { text: "×"; Accessible.name: "Закрыть редактор"; enabled: !editor.busy || !editor.initialized; onClicked: editor.dismiss() }
        }
        ColumnLayout {
            Layout.fillWidth: true; enabled: editor.initialized && !editor.busy; spacing: 6
            TextField {
                id: titleField; objectName: "playlistTitleField"
                Layout.fillWidth: true; implicitHeight: editor.mobile ? 48 : 52
                maximumLength: 255; placeholderText: "Название плейлиста"
                color: editor.host.ink; selectionColor: editor.host.accent; placeholderTextColor: editor.host.muted
                font.pixelSize: editor.mobile ? 24 : 28; font.weight: Font.DemiBold
                leftPadding: 0; rightPadding: 0
                Accessible.name: "Название плейлиста"
                background: Item {
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: editor.host.accent; opacity: titleField.activeFocus ? 0.6 : 0; Behavior on opacity { NumberAnimation { duration: editor.host.motion > 0 ? 120 : 0 } } }
                }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: editor.mobile ? 64 : 72
                TextArea {
                    id: description; objectName: "playlistDescriptionField"
                    placeholderText: "Добавьте описание"
                    color: editor.host.muted; selectionColor: editor.host.accent; placeholderTextColor: Qt.alpha(editor.host.muted, 0.8)
                    font.pixelSize: 13; wrapMode: TextEdit.Wrap; leftPadding: 0; rightPadding: 0; topPadding: 8; bottomPadding: 8
                    textFormat: TextEdit.PlainText; Accessible.name: "Описание плейлиста"
                    background: Item {}
                }
            }
            AbstractButton {
                id: privacy
                checkable: true; implicitHeight: 36; implicitWidth: privacyContent.implicitWidth + 20
                hoverEnabled: true; leftPadding: 10; rightPadding: 10
                Accessible.name: checked ? "Приватный плейлист. Изменить доступ" : "Публичный плейлист. Изменить доступ"
                ToolTip.visible: hovered; ToolTip.text: checked ? "Доступен только вам и по секретной ссылке" : "Виден всем в SoundCloud"
                background: Rectangle { radius: 18; color: Qt.alpha(editor.host.ink, privacy.down || privacy.hovered ? 0.09 : 0.045) }
                contentItem: RowLayout {
                    id: privacyContent; spacing: 7
                    Canvas {
                        Layout.preferredWidth: 14; Layout.preferredHeight: 14
                        property bool locked: privacy.checked
                        property color ink: editor.host.muted
                        onLockedChanged: requestPaint()
                        onInkChanged: requestPaint()
                        onPaint: {
                            const c = getContext("2d"); c.reset(); c.strokeStyle = ink; c.lineWidth = 1.2; c.lineCap = "round"; c.beginPath()
                            c.rect(3,6,8,6); c.moveTo(4,6); c.lineTo(4,4); c.bezierCurveTo(4,0,10,0,10,4); if (locked) c.lineTo(10,6); c.stroke()
                        }
                    }
                    Copy { text: privacy.checked ? "Приватный" : "Публичный"; color: editor.host.muted; font.pixelSize: 12 }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.alpha(editor.host.ink, 0.08) }
        RowLayout {
            Layout.fillWidth: true
            Copy { text: "Треки"; font.pixelSize: 14; font.weight: Font.DemiBold }
            Copy { text: (editor.draft.tracks || []).length; color: editor.host.muted; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Copy { text: "Потяните за ⠿"; color: editor.host.muted; font.pixelSize: 11 }
        }
        ListView {
            id: list; objectName: "playlistEditorTracks"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 80
            clip: true; boundsBehavior: Flickable.StopAtBounds
            model: sc.playlistEditModel
            enabled: !editor.busy
            interactive: dragFrom < 0
            readonly property int rowHeight: 64
            property int dragFrom: -1
            property int dragTo: -1
            property real pointerY: 0
            property real grabOffset: rowHeight / 2
            property var draggedTrack: ({})
            property bool settling: false
            property bool committing: false
            function startDrag(pressY, currentY) {
                if (editor.busy || settling) return
                const from = indexAt(1, contentY + pressY)
                if (from < 0) return
                dragFrom = from; dragTo = from
                draggedTrack = editor.draft.tracks[from]
                grabOffset = contentY + pressY - originY - from * rowHeight
                pointerY = currentY
                lifted.y = currentY - grabOffset
                updateTarget()
            }
            function updateTarget() {
                if (dragFrom < 0 || settling) return
                lifted.y = Math.max(-rowHeight / 2, Math.min(height - rowHeight / 2, pointerY - grabOffset))
                dragTo = Math.max(0, Math.min(count - 1, Math.floor((contentY + lifted.y + rowHeight / 2 - originY) / rowHeight)))
            }
            function finishDrag() {
                if (dragFrom < 0 || settling) return
                settling = true
                dropMotion.to = originY + dragTo * rowHeight - contentY
                dropMotion.start()
            }
            function cancelDrag() {
                dropMotion.stop(); settling = false; committing = true
                dragFrom = -1; dragTo = -1; committing = false
            }
            function commitDrag() {
                const from = dragFrom, to = dragTo
                committing = true; dragFrom = -1; settling = false
                if (from >= 0 && to >= 0 && from !== to) { editor.tracksChanged = true; sc.movePlaylistTrack(from, to) }
                committing = false
            }
            NumberAnimation { id: dropMotion; target: lifted; property: "y"; duration: editor.host.motion > 0 ? 160 : 0; easing.type: Easing.OutCubic; onFinished: list.commitDrag() }
            Timer {
                interval: 16; repeat: true; running: list.dragFrom >= 0 && !list.settling
                onTriggered: {
                    const edge = Math.min(56, list.height / 4)
                    const speed = list.pointerY < edge ? -Math.min(12, (edge - list.pointerY) * 0.2) : list.pointerY > list.height - edge ? Math.min(12, (list.pointerY - list.height + edge) * 0.2) : 0
                    list.contentY = Math.max(list.originY, Math.min(list.originY + Math.max(0, list.contentHeight - list.height), list.contentY + speed))
                    list.updateTarget()
                }
            }
            moveDisplaced: Transition { enabled: !list.committing; NumberAnimation { properties: "x,y"; duration: editor.host.motion > 0 ? 170 : 0; easing.type: Easing.OutCubic } }
            removeDisplaced: Transition { NumberAnimation { properties: "x,y"; duration: editor.host.motion > 0 ? 170 : 0; easing.type: Easing.OutCubic } }
            ScrollBar.vertical: ScrollBar { visible: list.dragFrom < 0 }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: list.width - 8; height: list.rowHeight
                opacity: list.dragFrom === index ? 0 : 1
                transform: Translate {
                    y: list.dragFrom >= 0 && row.index !== list.dragFrom ? (list.dragTo > list.dragFrom && row.index > list.dragFrom && row.index <= list.dragTo ? -list.rowHeight : list.dragTo < list.dragFrom && row.index >= list.dragTo && row.index < list.dragFrom ? list.rowHeight : 0) : 0
                    Behavior on y { enabled: !list.committing; NumberAnimation { duration: editor.host.motion > 0 ? 170 : 0; easing.type: Easing.OutCubic } }
                }
                Rectangle { anchors.fill: parent; radius: 10; color: rowHover.hovered ? Qt.alpha(editor.host.ink, 0.035) : "transparent" }
                HoverHandler { id: rowHover }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 6; spacing: 4
                    TrackContent { Layout.fillWidth: true; track: row.modelData; number: row.index + 1 }
                    QuietButton { text: "⋯"; implicitWidth: 36; Accessible.name: "Действия с треком «" + (row.modelData.title || "") + "»"; onClicked: { editor.selectedTrackIndex = row.index; rowActions.open() } }
                    AbstractButton {
                        objectName: "playlistDragHandle" + row.index
                        Layout.preferredWidth: 44; Layout.preferredHeight: list.rowHeight
                        Accessible.name: "Переместить «" + (row.modelData.title || "трек") + "». Alt и клавиши вверх или вниз"
                        focusPolicy: Qt.TabFocus
                        Keys.onPressed: function(event) {
                            if (!(event.modifiers & Qt.AltModifier)) return
                            const to = event.key === Qt.Key_Up ? row.index - 1 : event.key === Qt.Key_Down ? row.index + 1 : -1
                            if (to >= 0 && to < list.count) { editor.tracksChanged = true; sc.movePlaylistTrack(row.index, to); event.accepted = true }
                        }
                        contentItem: Canvas {
                            property color ink: editor.host.muted
                            onInkChanged: requestPaint()
                            onPaint: {
                                const c = getContext("2d"); c.reset(); c.fillStyle = ink
                                for (let x = -3; x <= 3; x += 6) for (let y = -5; y <= 5; y += 5) { c.beginPath(); c.arc(width / 2 + x, height / 2 + y, 1.1, 0, Math.PI * 2); c.fill() }
                            }
                        }
                    }
                }
            }
            // The pointer handler lives in the viewport, so a long drag survives
            // delegate recycling while auto-scrolling through a large playlist.
            Item {
                id: handleStrip
                parent: list
                anchors.right: parent.right; anchors.rightMargin: 8; width: 44; height: parent.height; z: 15
                enabled: !editor.busy && !list.settling
                HoverHandler { cursorShape: reorder.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
                DragHandler {
                    id: reorder
                    target: null; xAxis.enabled: false
                    acceptedButtons: Qt.LeftButton
                    onActiveChanged: {
                        if (active) list.startDrag(centroid.pressPosition.y, centroid.position.y)
                        else list.finishDrag()
                    }
                    onActiveTranslationChanged: if (active && list.dragFrom >= 0) { list.pointerY = centroid.position.y; list.updateTarget() }
                    onCanceled: list.cancelDrag()
                }
            }
            Rectangle {
                id: lifted
                parent: list; z: 20
                x: 2; width: list.width - 12; height: list.rowHeight
                visible: list.dragFrom >= 0
                radius: 12; color: editor.host.raised
                border.width: 1; border.color: Qt.alpha(editor.host.ink, 0.12)
                scale: list.settling ? 1 : 1.012
                Behavior on scale { NumberAnimation { duration: editor.host.motion > 0 ? 140 : 0; easing.type: Easing.OutCubic } }
                TrackContent { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 76; track: list.draggedTrack; floating: true }
            }
            Copy { anchors.centerIn: parent; text: editor.busy ? "Загрузка…" : "В плейлисте пока нет треков"; visible: !list.count; color: editor.host.muted }
        }
        Copy { Layout.fillWidth: true; text: editor.state.playlistEditError || ""; visible: !!text; wrapMode: Text.Wrap; elide: Text.ElideNone; color: editor.host.muted; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Copy { Layout.fillWidth: true; text: editor.dirty ? "Есть изменения" : "Всё сохранено"; color: editor.host.muted; font.pixelSize: 12 }
            SurfaceButton { host: editor.host; mobile: editor.mobile; text: "Повторить"; visible: !editor.initialized && !editor.busy; onClicked: sc.editPlaylist(editor.playlist) }
            SurfaceButton { host: editor.host; mobile: editor.mobile; primary: true; text: editor.busy ? "Подождите…" : "Сохранить"; enabled: editor.dirty && !editor.busy && list.dragFrom < 0 && !!titleField.text.trim(); onClicked: { Qt.inputMethod.hide(); sc.savePlaylist(titleField.text, description.text, privacy.checked) } }
        }
    }
    component Confirmation: Popup {
        parent: Overlay.overlay
        width: Math.min(400, parent.width - 32); x: (parent.width - width) / 2; y: (parent.height - height) / 2
        padding: 24; modal: true; focus: true; opacity: 0; scale: 0.985
        background: Rectangle { color: editor.host.panel; radius: 20; border.width: 1; border.color: Qt.alpha(editor.host.ink, 0.08) }
        Overlay.modal: Rectangle { color: "#99000000" }
        enter: SurfaceTransition { duration: editor.host.motion }
        exit: SurfaceTransition { duration: editor.host.motion; leaving: true }
    }
    Confirmation {
        id: options
        contentItem: ColumnLayout {
            spacing: 10
            Copy { text: "Плейлист"; font.weight: Font.DemiBold; font.pixelSize: 18 }
            SurfaceButton { host: editor.host; mobile: editor.mobile; quiet: true; Layout.fillWidth: true; text: "Удалить плейлист"; destructive: true; onClicked: { options.close(); confirmDelete.open() } }
            SurfaceButton { host: editor.host; mobile: editor.mobile; Layout.fillWidth: true; text: "Закрыть"; onClicked: options.close() }
        }
    }
    Confirmation {
        id: rowActions
        contentItem: ColumnLayout {
            spacing: 14
            Copy { Layout.fillWidth: true; text: (editor.draft.tracks || [])[editor.selectedTrackIndex]?.title || "Трек"; wrapMode: Text.Wrap; maximumLineCount: 2; font.weight: Font.DemiBold }
            SurfaceButton { host: editor.host; mobile: editor.mobile; quiet: true; Layout.fillWidth: true; text: "Удалить из плейлиста"; destructive: true; enabled: !editor.busy; onClicked: { sc.removePlaylistTrack(editor.selectedTrackIndex); editor.tracksChanged = true; rowActions.close() } }
            SurfaceButton { host: editor.host; mobile: editor.mobile; Layout.fillWidth: true; text: "Отмена"; onClicked: rowActions.close() }
        }
    }
    Confirmation {
        id: confirmDelete
        contentItem: ColumnLayout {
            spacing: 16
            Copy { text: "Удалить плейлист?"; font.pixelSize: 21; font.weight: Font.DemiBold }
            Copy { Layout.fillWidth: true; text: "«" + (editor.playlist.title || "Без названия") + "» будет удалён из SoundCloud. Отменить это действие нельзя."; wrapMode: Text.Wrap; elide: Text.ElideNone; color: editor.host.muted }
            RowLayout {
                Layout.fillWidth: true
                SurfaceButton { host: editor.host; mobile: editor.mobile; Layout.fillWidth: true; text: "Отмена"; onClicked: confirmDelete.close() }
                SurfaceButton { host: editor.host; mobile: editor.mobile; Layout.fillWidth: true; text: "Удалить"; destructive: true; onClicked: { confirmDelete.close(); sc.deletePlaylist() } }
            }
        }
    }
    Confirmation {
        id: discard
        contentItem: ColumnLayout {
            spacing: 16
            Copy { text: "Отменить изменения?"; font.pixelSize: 21; font.weight: Font.DemiBold }
            Copy { Layout.fillWidth: true; text: "Название, описание и состав плейлиста останутся прежними."; wrapMode: Text.Wrap; color: editor.host.muted; elide: Text.ElideNone }
            RowLayout {
                SurfaceButton { host: editor.host; mobile: editor.mobile; text: "Продолжить"; onClicked: discard.close() }
                SurfaceButton { host: editor.host; mobile: editor.mobile; text: "Отменить изменения"; onClicked: { discard.close(); editor.initialized = false; editor.close() } }
            }
        }
    }
}
