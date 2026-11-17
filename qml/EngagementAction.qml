import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

SurfaceButton {
    id: control
    property var item: ({})
    property bool repost: false
    property var backend: sc
    readonly property string itemUrn: item?.urn || ""
    readonly property bool track: itemUrn.startsWith("soundcloud:tracks:")
    readonly property string kind: repost ? (track ? "trackReposts" : "playlistReposts") : track ? "trackLikes" : itemUrn.startsWith("soundcloud:system-playlists:") ? "stationLikes" : "playlistLikes"
    readonly property var api: backend.uiState
    readonly property bool ready: track && !repost || !!api.engagementReady?.[kind]
    readonly property bool loading: !!api.engagementLoading?.[kind]
    readonly property bool pending: track && !repost ? !!api.likePending?.[itemUrn] : !!api.engagementPending?.[kind + ":" + itemUrn]
    selected: track && !repost ? (api.engagementStates?.[itemUrn]?.like ?? !!item.user_favorite) : !!api.engagement?.[kind]?.[itemUrn]
    readonly property bool hasPeople: track || (repost && itemUrn.startsWith("soundcloud:playlists:"))
    readonly property string countField: repost ? "reposts_count" : track ? "favoritings_count" : "likes_count"
    readonly property var count: api.engagementCounts?.[itemUrn]?.[countField] ?? item[countField]
    readonly property bool countKnown: item.reveal_stats !== false && count !== undefined && count !== null && Number.isFinite(Number(count)) && Number(count) >= 0
    readonly property string fullCountText: countKnown ? Number(count).toLocaleString(Qt.locale(), "f", 0) : "—"
    readonly property string countText: {
        if (!countKnown) return "—"
        const value = Number(count)
        const divisor = value >= 1e9 ? 1e9 : value >= 1e6 ? 1e6 : value >= 1e4 ? 1e3 : 1
        return divisor === 1 ? fullCountText : (Math.floor(value / divisor * 10) / 10).toLocaleString(Qt.locale(), "f", 1) + (divisor === 1e9 ? "B" : divisor === 1e6 ? "M" : "K")
    }
    readonly property string description: (repost ? (selected ? "Отменить репост" : "Сделать репост") : (selected ? "Убрать из любимых" : "Добавить в любимые")) + "; " + (countKnown ? (repost ? "репостов: " : "лайков: ") + fullCountText : item.reveal_stats === false ? "счётчик скрыт автором" : "количество неизвестно") + "; " + (selected ? (repost ? "ваш репост установлен" : "ваш лайк установлен") : (repost ? "вашего репоста нет" : "вашего лайка нет"))
    property bool held: false
    property string pressedUrn: ""
    visible: track || itemUrn.startsWith("soundcloud:playlists:") || (!repost && itemUrn.startsWith("soundcloud:system-playlists:"))
    text: countText
    quiet: true
    leftPadding: 10; rightPadding: 10
    implicitWidth: Math.max(mobile ? 48 : 44, contents.implicitWidth + 20)
    Accessible.name: description
    Accessible.description: hasPeople ? "Удерживайте или откройте контекстное меню для списка пользователей" : ""
    Accessible.checkable: true
    Accessible.checked: selected
    ToolTip.visible: hovered
    ToolTip.text: description
    contentItem: RowLayout {
        id: contents
        spacing: 6
        Loader {
            id: glyph
            Layout.preferredWidth: 18; Layout.preferredHeight: 18
            // CMake packages shared QML in a flat module; source-tree tests
            // retain the android subdirectory.
            source: Qt.resolvedUrl(Qt.resolvedUrl(".").toString().endsWith("/qml/") ? "android/AndroidGlyph.qml" : "AndroidGlyph.qml")
        }
        Binding { target: glyph.item; property: "name"; value: control.repost ? "repeat" : control.selected ? "liked" : "heart"; when: glyph.status === Loader.Ready }
        Binding { target: glyph.item; property: "ink"; value: control.selected ? control.host.accent : control.host.ink; when: glyph.status === Loader.Ready }
        Label { objectName: "engagementCount"; text: control.countText; font.pixelSize: 12; color: control.selected ? control.host.accent : control.host.ink; elide: Text.ElideRight; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
    }
    function sync() { if (visible && !(track && !repost)) backend.loadEngagement(kind) }
    function showPeople() { if (hasPeople) host.openEngagementPeople(item, repost ? "reposters" : "favoriters") }
    onItemUrnChanged: { held = false; Qt.callLater(control.sync) }
    onVisibleChanged: if (visible) Qt.callLater(control.sync)
    Component.onCompleted: Qt.callLater(control.sync)
    Connections { target: control.backend; function onSignedInChanged() { control.sync() } }
    // AbstractButton lets a parent Flickable steal a drag. A held gesture is
    // consumed even on platforms that subsequently deliver clicked().
    onPressed: { held = false; pressedUrn = itemUrn }
    onPressAndHold: { held = true; if (pressedUrn === itemUrn) showPeople() }
    onClicked: {
        if (held || pressedUrn !== itemUrn || pending || loading) return
        if (!ready && backend.signedIn) { backend.loadEngagement(kind); return }
        if (repost) backend.toggleRepost(item); else backend.like(item)
    }
    Keys.onPressed: function(event) {
        if (hasPeople && (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)))) {
            menu.popup(); event.accepted = true
        }
    }
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: if (control.hasPeople) menu.popup() }
    Menu {
        id: menu
        MenuItem { text: control.repost ? "Кто сделал репост" : "Кто поставил лайк"; onTriggered: control.showPeople() }
    }
}
