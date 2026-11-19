import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: popup
    required property var host
    property bool mobile: false
    property real safeBottomPadding: 0
    property var playlist: ({})
    readonly property bool owned: (playlist.urn || "").startsWith("soundcloud:playlists:") && !!state.profile?.urn && (playlist.user?.urn || playlist.user_urn) === state.profile.urn
    property real reveal: 0
    readonly property int transitionDuration: host.motion > 0 ? Math.max(280, host.motion) : 0
    opacity: 0
    // Transform the surface as one piece without moving its layout or hit area.
    onContentItemChanged: if (contentItem) contentItem.transform = [surfaceOffset]
    onBackgroundChanged: if (background) background.transform = [surfaceOffset]
    Translate { id: surfaceOffset; y: (1 - popup.reveal) * (popup.mobile ? 40 : 12) }
    Behavior on height {
        enabled: popup.visible && popup.transitionDuration > 0
        NumberAnimation { duration: popup.transitionDuration; easing.type: Easing.InOutCubic }
    }
    readonly property var state: sc.uiState
    readonly property var songs: collectionTools.rows
    function begin(item) {
        popupSelection.reset()
        playlist = item
        sc.loadPlaylist(item)
        open()
        list.positionViewAtBeginning()
    }
    function beginStation(seed, artist = true) {
        const owner = seed.user || seed
        const stationUrn = (artist ? owner.station_urn : seed.station_urn) || ""
        if (stationUrn.startsWith("soundcloud:system-playlists:")) {
            sc.openStation(stationUrn, seed)
            return
        }
        begin({urn: seed.urn, preview_type: "station", seed: seed,
               title: qsTr("Подборка SCrip · ") + (seed.title || seed.user?.username || qsTr("Похожие треки")),
               artwork_url: artist ? seed.user?.avatar_url || seed.artwork_url : seed.artwork_url,
               user: {username: qsTr("Похожие треки · SoundCloud")}})
    }
    function beginMix(collection, tracks) {
        begin({preview_type: "mix", title: collection.title || qsTr("Подборка"),
               tracks: tracks, track_count: tracks.length,
               artwork_url: collection.artwork_url || tracks[0]?.artwork_url,
               user: {username: collection.subtitle || qsTr("Подборка SCrip")}})
    }
    TrackSelection { id: popupSelection; rows: popup.songs; context: (collectionTools.transformed ? collectionTools.sourceRows.length : "") + ":" + collectionTools.sortKey + ":" + collectionTools.descending + ":" + collectionTools.query + ":" + (popup.playlist.urn || popup.playlist.title || "") + ":" + (popup.state.profile?.urn || "") + ":" + sc.signedIn }
    function selectionBack() { return visible && selectionBar.back() }
    onClosed: { popupSelection.reset(); sc.cancelCollectionSearch() }
    function playAt(index) { sc.playMix(songs, index) }
    width: Math.min(parent.width - (mobile ? 0 : 64), 680)
    height: Math.min(parent.height - (mobile ? 36 : 64),
                     (mobile ? 375 : 415) + selectionBar.implicitHeight + 16 + Math.max(1, Math.min(playlist.track_count ?? (state.playlistBusy ? 4 : songs.length), 7)) * (mobile ? 72 : 62) + safeBottomPadding)
    x: (parent.width - width) / 2
    y: mobile ? parent.height - height : (parent.height - height) / 2
    padding: mobile ? 20 : 28
    bottomPadding: padding + safeBottomPadding
    modal: true
    focus: true
    closePolicy: popupSelection.active ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: host.panel; radius: mobile ? 24 : 18; border.width: mobile ? 0 : 1; border.color: host.raised }
    Overlay.modal: Rectangle {
        color: "#99000000"
        Behavior on opacity {
            NumberAnimation { duration: popup.transitionDuration; easing.type: Easing.OutCubic }
        }
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 1; duration: popup.transitionDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "reveal"; to: 1; duration: popup.transitionDuration; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: popup.transitionDuration * 0.75; easing.type: Easing.InOutCubic }
            NumberAnimation { property: "reveal"; to: 0; duration: popup.transitionDuration * 0.75; easing.type: Easing.InOutCubic }
        }
    }

    component Copy: Text {
        color: host.ink
        font.pixelSize: 14
        textFormat: Text.PlainText
        elide: Text.ElideRight
    }
    component Command: Button {
        id: button
        property bool primary: false
        implicitHeight: mobile ? 48 : 38
        leftPadding: 16; rightPadding: 16
        hoverEnabled: true
        contentItem: Text {
            text: button.text; font.pixelSize: 13; font.weight: Font.Medium
            color: button.primary ? host.foreground(host.accent) : host.ink
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 10
            color: button.primary ? host.accent : host.raised
            opacity: !button.enabled ? 0.4 : button.down ? 0.65 : button.hovered ? 0.8 : 1
        }
    }
    PlaylistEditor {
        id: editor
        host: popup.host; mobile: popup.mobile
        onSaved: function(item) { popup.playlist = item; if (typeof popup.host.refreshPage === "function") popup.host.refreshPage(true); else popup.host.refresh(true) }
        onDeleted: function(urn) { popup.close(); if (typeof popup.host.refreshPage === "function") popup.host.refreshPage(true); else popup.host.refresh(true) }
    }
    function dismissEditor() { if (editor.visible) { editor.dismiss(); return true } if (playlistMenu.visible) { playlistMenu.close(); return true } return false }
    Connections {
        target: sc
        function onPlaylistUpdated(item) { if (popup.playlist.urn === item.urn) popup.playlist = item }
        function onPlaylistDeleted(urn) { if (popup.playlist.urn === urn) popup.close() }
    }
    Popup {
        id: playlistMenu
        parent: Overlay.overlay
        width: Math.min(350, parent.width - 32); x: (parent.width - width) / 2; y: (parent.height - height) / 2
        padding: 12; modal: true; focus: true
        background: Rectangle { color: host.panel; radius: 18; border.width: 1; border.color: host.raised }
        Overlay.modal: Rectangle { color: "#66000000" }
        contentItem: ColumnLayout {
            spacing: 8
            EngagementAction { host: popup.host; mobile: popup.mobile; Layout.fillWidth: true; item: popup.playlist; repost: true }
            SurfaceButton { host: popup.host; mobile: popup.mobile; Layout.fillWidth: true; text: qsTr("Редактировать плейлист"); visible: popup.owned; onClicked: { playlistMenu.close(); editor.begin(popup.playlist) } }
            SurfaceButton { host: popup.host; mobile: popup.mobile; Layout.fillWidth: true; text: qsTr("Профиль автора"); visible: !!popup.playlist.user?.urn; onClicked: { playlistMenu.close(); popup.close(); popup.host.openProfile(popup.playlist.user) } }
            SurfaceButton { host: popup.host; mobile: popup.mobile; Layout.fillWidth: true; text: qsTr("Копировать ссылку"); visible: !!popup.playlist.permalink_url; onClicked: { sc.copyText(popup.playlist.permalink_url); playlistMenu.close() } }
            SurfaceButton { host: popup.host; mobile: popup.mobile; Layout.fillWidth: true; text: qsTr("Закрыть"); onClicked: playlistMenu.close() }
        }
    }
    contentItem: ColumnLayout {
        Keys.onEscapePressed: event => { event.accepted = selectionBar.back() }
        spacing: 16
        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: popup.mobile ? Math.min(previewHeader.implicitHeight, Math.max(80, popup.availableHeight - selectionBar.implicitHeight - 104)) : previewHeader.implicitHeight
            Layout.minimumHeight: popup.mobile ? 80 : previewHeader.implicitHeight
            contentWidth: width; contentHeight: previewHeader.implicitHeight
            clip: true; boundsBehavior: Flickable.StopAtBounds
            interactive: popup.mobile && contentHeight > height
            ScrollBar.vertical: ScrollBar { policy: parent.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
            ColumnLayout {
                id: previewHeader; width: parent.width; spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    Copy { Layout.fillWidth: true; text: (popup.playlist.urn || "").startsWith("soundcloud:system-playlists:") ? qsTr("СТАНЦИЯ SOUNDCLOUD") : popup.playlist.preview_type === "station" || popup.playlist.preview_type === "mix" ? qsTr("ПОДБОРКА SCrip") : popup.playlist.is_album || popup.playlist.playlist_type === "album" ? qsTr("АЛЬБОМ") : qsTr("ПЛЕЙЛИСТ"); font.pixelSize: 11; font.letterSpacing: 1.5; color: host.muted }
                    Command { text: qsTr("Ещё"); visible: !!popup.playlist.urn; onClicked: playlistMenu.open() }
                    Command { text: qsTr("Закрыть"); onClicked: popup.close() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: mobile ? 16 : 24
                    Rectangle {
                        Layout.preferredWidth: mobile ? 104 : 148
                        Layout.preferredHeight: width
                        radius: mobile ? host.coverRadius : host.artworkRadius
                        color: host.raised
                        Copy { anchors.centerIn: parent; text: "♫"; color: host.muted; font.pixelSize: 32 }
                        Artwork { id: coverImage; anchors.fill: parent; radius: parent.radius; source: host.cover(popup.playlist); visible: !!source }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Copy { Layout.fillWidth: true; text: popup.playlist.title || qsTr("Без названия"); font.pixelSize: mobile ? 20 : 26; font.weight: Font.DemiBold; wrapMode: Text.Wrap; maximumLineCount: 3 }
                        Copy { Layout.fillWidth: true; text: popup.playlist.user?.username || "SoundCloud"; color: host.muted }
                        Copy { Layout.fillWidth: true; text: (popup.playlist.track_count ?? popup.songs.length) + qsTr(" треков"); font.pixelSize: 12; color: host.muted }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Command { Layout.fillWidth: mobile; text: qsTr("Слушать"); primary: true; enabled: popup.songs.length > 0 && !popup.state.playlistError && !popup.state.playlistBusy; onClicked: popup.playAt(0) }
                    Command { Layout.fillWidth: mobile; text: qsTr("Перемешать"); enabled: popup.songs.length > 0 && !popup.state.playlistError && !popup.state.playlistBusy; onClicked: { const shuffled = popup.songs.slice(); for (let i = shuffled.length - 1; i > 0; --i) { const j = Math.floor(Math.random() * (i + 1)); const t = shuffled[i]; shuffled[i] = shuffled[j]; shuffled[j] = t } sc.playMix(shuffled, 0) } }
                    Item { visible: !mobile; Layout.fillWidth: true }
                    Command { visible: !mobile && !!popup.playlist.permalink_url; text: qsTr("Поделиться"); onClicked: sc.copyText(popup.playlist.permalink_url) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    EngagementAction { host: popup.host; mobile: popup.mobile; item: popup.playlist }
                    Copy { Layout.fillWidth: true; visible: popup.playlist.sharing === "private"; text: qsTr("Приватный плейлист"); color: host.muted; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: host.raised }
                CollectionTools {
                    id: collectionTools; Layout.fillWidth: true; host: popup.host; backend: sc; mobile: popup.mobile
                    playlistContext: true; contextKey: "playlist:" + (popup.playlist.urn || popup.playlist.title || "")
                }
            }
        }
        SelectionBar { id: selectionBar; Layout.fillWidth: true; host: popup.host; mobile: popup.mobile; selection: popupSelection }
        ListView {
            id: list
            objectName: "previewTrackList"
            Layout.minimumHeight: popup.mobile ? 72 : 62
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: popup.songs.length
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property int index
                readonly property var modelData: popup.songs[index] || ({})
                SelectionInput { anchors.fill: parent; z: 20; selection: popupSelection; rowIndex: row.index; host: popup.host; mobile: popup.mobile }
                readonly property bool current: modelData.urn === popup.state.current?.urn
                width: list.width
                height: mobile ? 72 : 62
                padding: 8
                onPressAndHold: if (popup.mobile) popupSelection.choose(index)
                onClicked: if (!popupSelection.active) popup.playAt(index)
                background: Rectangle { radius: 10; color: row.current ? Qt.alpha(host.accent, 0.12) : row.hovered || row.down ? host.raised : "transparent" }
                contentItem: RowLayout {
                    spacing: 12
                    Copy { Layout.preferredWidth: 24; text: row.current ? "♪" : row.index + 1; color: row.current ? host.accent : host.muted; horizontalAlignment: Text.AlignHCenter }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Copy { Layout.fillWidth: true; text: row.modelData.title || qsTr("Без названия"); color: row.current ? host.accent : host.ink; font.weight: Font.Medium }
                        Copy { Layout.fillWidth: true; text: sc.displayArtist(row.modelData) || "SoundCloud"; color: host.muted; font.pixelSize: 12 }
                    }
                    Copy { visible: !mobile; text: host.time(row.modelData.duration); color: host.muted; font.pixelSize: 12 }
                    AbstractButton {
                        objectName: "previewTrackActions" + row.index
                        implicitWidth: mobile ? 48 : 38; implicitHeight: mobile ? 48 : 38
                        hoverEnabled: true
                        Accessible.name: qsTr("Действия с треком")
                        onClicked: host.openTrackActions(row.modelData)
                        background: Rectangle { radius: 10; color: parent.down || parent.hovered ? Qt.alpha(host.ink, 0.1) : host.raised }
                        contentItem: Canvas {
                            property color ink: host.ink
                            onInkChanged: requestPaint()
                            onPaint: {
                                const c = getContext("2d"); c.reset()
                                c.strokeStyle = ink; c.lineWidth = 1.6; c.lineCap = "round"
                                c.beginPath()
                                for (let y = -6; y <= 6; y += 6) { c.moveTo(width / 2 - 8, height / 2 + y); c.lineTo(width / 2 + 8, height / 2 + y) }
                                c.stroke()
                            }
                        }
                    }
                }
            }
            footer: Column {
                width: list.width
                spacing: 8
                Copy { width: parent.width; visible: !!popup.state.playlistError; text: popup.state.playlistError || ""; wrapMode: Text.Wrap; elide: Text.ElideNone; color: host.muted }
                Command { anchors.horizontalCenter: parent.horizontalCenter; visible: !popup.state.playlistBusy && (!!popup.state.playlistError || !!popup.state.playlistHasMore); text: popup.state.playlistError ? qsTr("Повторить") : qsTr("Загрузить ещё"); onClicked: sc.loadPlaylist(popup.playlist, !!popup.state.playlistHasMore) }
                BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; visible: running; running: !!popup.state.playlistBusy; height: 44 }
                Copy { width: parent.width; horizontalAlignment: Text.AlignHCenter; visible: !popup.state.playlistBusy && !popup.state.playlistError && !popup.songs.length; text: collectionTools.query.trim() ? (popup.state.playlistHasMore || popup.state.collectionSearchStatus === "error" || popup.state.collectionSearchStatus === "limit" ? qsTr("В загруженной части совпадений нет") : qsTr("Ничего не найдено")) : qsTr("В этом плейлисте пока нет треков"); color: host.muted }
            }
        }
    }
}
