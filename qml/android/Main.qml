import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import QtCore
import SCrip.Native 1.0
import QtQuick.Dialogs as Dialogs

ApplicationWindow {
    id: app
    objectName: "applicationWindow"
    visible: true
    width: 412
    height: 860
    minimumWidth: 320
    minimumHeight: 480
    title: "Sange · Android"
    color: app.surface
    palette.window: app.surface
    palette.windowText: app.ink
    palette.text: app.ink
    palette.base: app.surface
    palette.button: app.raised
    palette.buttonText: app.ink
    palette.highlight: app.accent
    palette.highlightedText: app.foreground(app.accent)
    readonly property var api: sc.uiState
    readonly property string homeAccount: sc.signedIn ? String(api.profile?.urn || "") : ""
    onHomeAccountChanged: if (homeAccount) Qt.callLater(function() {
        // Profile readiness invalidates recommendations started during session restoration.
        if (sc.signedIn && app.homeAccount) sc.loadHome(true)
    })
    readonly property bool dark: prefs.darkMode
    onDarkChanged: if (androidMedia) androidMedia.setDarkTheme(dark)
    property bool requestedDark: prefs.darkMode
    function switchTheme(value) {
        requestedDark = value
        if (!prefs.motion) {
            themeTransition.stop()
            themeVeil.opacity = 0
            prefs.darkMode = value
        } else if (!themeTransition.running && value !== prefs.darkMode) {
            themeTransition.start()
        }
    }
    Rectangle {
        id: themeVeil
        parent: Overlay.overlay
        anchors.fill: parent
        z: 10000
        color: "#111111"
        opacity: 0
        visible: opacity > 0
    }
    SequentialAnimation {
        id: themeTransition
        NumberAnimation { target: themeVeil; property: "opacity"; to: 0.16; duration: app.motion / 2; easing.type: Easing.InOutQuad }
        ScriptAction { script: prefs.darkMode = app.requestedDark }
        NumberAnimation { target: themeVeil; property: "opacity"; to: 0; duration: app.motion / 2; easing.type: Easing.InOutQuad }
        onFinished: if (app.requestedDark !== prefs.darkMode) start()
    }

    Connections {
        target: prefs
        function onMotionChanged() {
            if (!prefs.motion) {
                if (themeTransition.running) {
                    themeTransition.stop()
                    prefs.darkMode = app.requestedDark
                }
                themeVeil.opacity = 0
                pageArrival.stop()
                body.opacity = 1
                pageOffset.y = 0
            }
        }
    }

    readonly property color baseAccent: prefs.accentOverride ? prefs.accent : pluginManager.theme.accent || prefs.accent
    property color accent: (dark ? Qt.tint(baseAccent, Qt.rgba(1, 1, 1, 0.28)) : baseAccent)
    property color surface: (dark ? "#111111" : "#fafafa")
    property color panel: (dark ? "#1c1c1c" : "#ffffff")
    property color raised: (dark ? "#2b2b2b" : "#eeeeee")
    function foreground(c) {
        function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b) > 0.179 ? "#111113" : "#ffffff"
    }
    readonly property real coverRadius: prefs.coverRadiusOverride ? prefs.coverRadius : pluginManager.theme.coverRadius ?? prefs.coverRadius
    property color ink: (dark ? "#f4f4f4" : "#242424")
    Behavior on surface { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    Behavior on panel { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    Behavior on raised { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    Behavior on ink { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    Behavior on accent { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    readonly property color muted: (dark ? "#aaaaaa" : "#747474")
    // SafeArea появился в Qt 6.9. Остальные версии используют нулевые отступы.
    function safeAreaTop(item) {
        return typeof SafeArea !== "undefined" ? item.SafeArea.margins.top : 0
    }
    function safeAreaBottom(item) {
        return typeof SafeArea !== "undefined" ? item.SafeArea.margins.bottom : 0
    }
    readonly property real headerTopInset: safeAreaTop(app)
    Settings {
        id: prefs
        category: "androidInterface"
        property bool themePreferencesMigrated: false
        Component.onCompleted: {
            if (!themePreferencesMigrated) {
                accentOverride = accent !== "#f43756"
                coverRadiusOverride = coverRadius !== 16
                themePreferencesMigrated = true
            }
        }
        property bool timedComments: false
        property bool showComments: true
        property bool showTooltips: false
        property bool commentsLeft: false
        property bool darkMode: true
        property bool accentOverride: false
        property bool coverRadiusOverride: false
        property string accent: "#f43756"
        property bool motion: true
        property bool autoLoad: true
        property int coverRadius: 16
    }
    property string message: ""
    property var guest: null
    property int profileTab: 0
    property string profileLikes: "likes"
    readonly property string currentCreditKey: JSON.stringify([api.current?.urn, api.current?.title, sc.displayArtist(api.current || {})])
    onCurrentCreditKeyChanged: {
        if (selectedTrack?.urn && selectedTrack.urn === api.current?.urn) selectedTrack = api.current
        if (lyricTrack?.urn && lyricTrack.urn === api.current?.urn) {
            lyricTrack = api.current
            if (trackMenu.opened || lyricsSheet.opened) lyricsBackend.lookup(lyricTrack.title || "", sc.displayArtist(lyricTrack), lyricTrack.duration || 0, String(lyricTrack.urn || ""))
        }
    }
    property string repostLibrary: "reposts"
    readonly property var profile: guest ? (api.visitedProfile?.urn === guest.urn ? api.visitedProfile : guest) : api.profile
    function openTrackActions(track) { selectedTrack = track; selectedQueue = -1; trackMenu.open() }
    function openProfile(user) {
        pageHistory.push({page: page, guest: guest, profileTab: profileTab})
        navigatingBack = true
        guest = user && user.urn !== api.profile?.urn ? user : null
        profileTab = 0; page = 4; previousPage = page
        navigatingBack = false
        refresh()
    }

    property var pageHistory: []
    property int previousPage: 0
    property bool navigatingBack: false
    function goBack() {
        if (trackInfoSheet.visible) { trackInfoSheet.close(); return true }
        if (playlistView.dismissEditor()) return true
        if (peoplePopup.visible) { peoplePopup.close(); return true }
        if (commentsSheet.visible) { commentsSheet.close(); return true }
        if (recommendationSettings.visible) { recommendationSettings.close(); return true }
        if (trackMenu.visible) { trackMenu.close(); return true }
        if (playlistView.visible) { if (!playlistView.selectionBack()) playlistView.close(); return true }
        if (lyricsSheet.visible) { lyricsSheet.close(); return true }
        if (player.visible) { player.close(); return true }
        if (selectionBar.back()) return true
        if (pageHistory.length) {
            navigatingBack = true
            const entry = pageHistory.pop()
            guest = entry.guest; profileTab = entry.profileTab; page = entry.page
            previousPage = page
            navigatingBack = false
            refresh()
            return true
        }
        return false
    }
    onClosing: function(close) { if (goBack()) close.accepted = false }
    property int page: 0
    property string library: "likes"
    property string searchKind: "tracks"
    readonly property var lyricState: lyricsBackend.state
    property var lyricTrack: ({})
    property var selectedTrack: ({})
    property int selectedQueue: -1
    function cover(item) { return (pluginManager.theme.covers === "artist" ? item?.user?.avatar_url || item?.artwork_url || "" : item?.artwork_url || item?.tracks?.[0]?.artwork_url || item?.user?.avatar_url || "").replace("-large.", "-t500x500.") }
    readonly property string commentsTrack: sc.signedIn && (player.visible || commentsSheet.visible)
        ? String((commentsSheet.visible ? commentsSheet.track : api.current).urn || "") : ""
    onCommentsTrackChanged: if (typeof sc.loadComments === "function") sc.loadComments(commentsTrack)
    readonly property var commentState: sc.commentsState || ({})
    readonly property var timedTrackComments: (commentState.track === api.current.urn ? (commentState.items || []) : [])
        .filter(c => c.timestamp >= 0).slice().sort((a, b) => a.timestamp - b.timestamp)
    readonly property var playingComment: {
        let low = 0, high = timedTrackComments.length
        while (low < high) { const mid = Math.floor((low + high) / 2); if (timedTrackComments[mid].timestamp <= sc.position) low = mid + 1; else high = mid }
        const c = timedTrackComments[low - 1]
        return c && sc.position - c.timestamp < 8000 ? c : ({})
    }
    Timer {
        interval: 450; repeat: true
        running: player.visible && app.active && !commentsSheet.visible && !!app.commentState.hasMore && !app.commentState.busy && !app.commentState.error && (app.commentState.items || []).length < 5000
        onTriggered: sc.loadComments(app.api.current.urn, true)
    }
    function time(ms) { let s = Math.floor((ms || 0) / 1000); return Math.floor(s / 60) + ":" + (s % 60).toString().padStart(2, "0") }
    function searchNow() {
        Qt.inputMethod.hide()
        if (/^(https?:\/\/|scrip:|soundcloud:)/i.test(search.text.trim()) || searchKind !== "tracks") refresh()
        else trackFilters.apply()
    }
    function refresh(force = false) {
        if (!sc.signedIn) return
        if (page === 0) sc.loadHome(force)
        else if (page === 1) sc.browse("feed", "", force)
        else if (page === 2) { if (/^(https?:\/\/|scrip:|soundcloud:)/i.test(search.text.trim())) sc.openLink(search.text); else sc.browse(searchKind, search.text, force) }
        else if (page === 3) sc.browse(library, "", force)
        else if (page === 4) sc.browse(guest ? ["userTracks", "userSets", {likes: "userLikes", likedPlaylists: "userLikedPlaylists", likedStations: "userLikedStations"}[profileLikes], repostLibrary === "reposts" ? "userReposts" : "userRepostedPlaylists"][profileTab] : ["mine", "playlists", profileLikes, repostLibrary][profileTab], guest?.urn || "", force)
    }
    readonly property bool showTooltips: prefs.showTooltips
    readonly property int motion: prefs.motion ? 280 : 0
    // Collection navigation animates once; typing and filter edits do not retrigger it.
    readonly property string arrivalKey: page + ":" + (page === 3 ? library : page === 2 ? searchKind : page === 4 ? profileTab + ":" + profileLikes + ":" + repostLibrary + ":" + (guest?.urn || "me") : "")
    onArrivalKeyChanged: if (prefs.motion && !pageArrival.running) pageArrival.start()
    property var scrollPositions: ({})
    readonly property string scrollKey: page + ":" + (page === 3 ? library : page === 2 ? searchKind + ":" + search.text + ":" + (searchKind === "tracks" ? trackFilters.summary : "") : page === 4 ? profileTab + ":" + profileLikes + ":" + repostLibrary + ":" + (guest?.urn || "me") : "")
    property string activeScrollKey: scrollKey
    property bool restoringScroll: false
    property bool profileTabResetPending: false
    onScrollKeyChanged: {
        restoringScroll = true
        activeScrollKey = scrollKey
        const saved = scrollPositions[activeScrollKey] || 0
        Qt.callLater(function() {
            if (app.page === 4 && app.profileTabResetPending)
                tracks.positionViewAtBeginning()
            else
                tracks.contentY = saved
            restoringScroll = false
        })
    }
    onPageChanged: { if (!navigatingBack) pageHistory.push({page: previousPage, guest: guest, profileTab: profileTab}); previousPage = page; refresh(); if (prefs.motion && !pageArrival.running) pageArrival.start() }
    ParallelAnimation {
        id: pageArrival
        NumberAnimation { target: body; property: "opacity"; from: 0.9; to: 1; duration: app.motion; easing.type: Easing.OutCubic }
        NumberAnimation { target: pageOffset; property: "y"; from: 5; to: 0; duration: app.motion; easing.type: Easing.OutQuint }
    }
    function openEngagementPeople(item, kind) { peoplePopup.begin(item, kind) }
    PeoplePopup {
        id: peoplePopup
        host: app; mobile: true
        onProfileRequested: function(user) { playlistView.close(); player.close(); app.openProfile(user) }
    }
    Connections {
        target: sc
        function onResourceResolved(item) {
            if ((item.urn || "").startsWith("soundcloud:users:")) app.openProfile(item)
            else if (item.preview_type === "station" || (item.urn || "").startsWith("soundcloud:playlists:") || (item.urn || "").startsWith("soundcloud:system-playlists:")) playlistView.begin(item)
            else { sc.playMix([item], 0); player.open() }
        }
    }
    PlaylistPopup {
        id: playlistView
        parent: Overlay.overlay
        host: app
        mobile: true
        safeBottomPadding: app.safeAreaBottom(app)
    }
    component FineSlider: Slider {
        id: fine
        implicitHeight: 40
        leftPadding: 0; rightPadding: 0
        background: Rectangle {
            x: fine.leftPadding; y: fine.topPadding + fine.availableHeight / 2 - height / 2
            width: fine.availableWidth; height: 3; radius: 1.5; color: (app.dark ? "#484848" : "#dcdcdc")
            Rectangle { width: fine.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: app.accent }
        }
        handle: Rectangle {
            x: fine.leftPadding + fine.visualPosition * (fine.availableWidth - width)
            y: fine.topPadding + fine.availableHeight / 2 - height / 2
            width: 12; height: 12; radius: 6; color: app.accent
            scale: fine.pressed && app.motion > 0 ? 1.35 : 1
            Behavior on scale { NumberAnimation { duration: app.motion * 0.5; easing.type: Easing.OutCubic } }
        }
    }
    component PlayerButton: AbstractButton {
        
        id: transport
        hoverEnabled: true
        property string symbol: ""
        property bool active: false
        property bool main: false
        property bool loading: false
        implicitWidth: main ? 76 : 48
        implicitHeight: main ? 76 : 48
        Accessible.name: text
        scale: down && app.motion > 0 ? 0.965 : 1
        Behavior on scale { NumberAnimation { duration: app.motion * (down ? 0.25 : 0.65); easing.type: Easing.OutCubic } }
        background: Rectangle {
            radius: width / 2
            color: transport.main ? app.accent : transport.down ? (app.dark ? "#383838" : "#e6e6e6") : "transparent"
            Behavior on color { ColorAnimation { duration: prefs.motion ? 150 : 0 } }
        }
        contentItem: Item {
            AndroidGlyph { visible: !transport.loading; anchors.centerIn: parent; width: transport.main ? 26 : 22; height: width; name: transport.symbol; ink: transport.main ? app.foreground(app.accent) : transport.active ? app.accent : app.ink }
            BusyIndicator { anchors.centerIn: parent; width: 32; height: 32; running: transport.loading; visible: running }
            Rectangle { visible: transport.active && !transport.main; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; width: 3; height: 3; radius: 1.5; color: app.accent }
        }
    }
    component Sheet: Popup {
        id: sheet
        property real reveal: 0
        property bool unifiedSurface: false
        property real dragOffset: 0
        property bool dragging: false
        property bool manualMotion: false
        property bool swipeDismissEnabled: true
        property real swipeHeaderHeight: topPadding + 48
        SwipeDismiss {
            targetPopup: sheet
            enabled: sheet.swipeDismissEnabled
            headerHeight: sheet.swipeHeaderHeight
            entranceOffset: sheet.unifiedSurface ? 0 : (1 - sheet.reveal) * sheet.height + sheet.dragOffset
        }
        opacity: 0
        Behavior on dragOffset {
            enabled: !sheet.dragging && !sheet.manualMotion
            SpringAnimation { spring: 4; damping: 0.32; epsilon: 0.25 }
        }
        enter: Transition {
            enabled: !sheet.manualMotion
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 1; duration: sheet.manualMotion || !app.motion ? 0 : 280; easing.type: Easing.OutCubic }
                NumberAnimation { property: "reveal"; to: 1; duration: sheet.manualMotion || !app.motion ? 0 : 320; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition {
            enabled: !sheet.manualMotion
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: sheet.manualMotion ? 0 : app.motion * 0.8; easing.type: Easing.InQuad }
                NumberAnimation { property: "reveal"; to: 0; duration: sheet.manualMotion ? 0 : app.motion * 0.8; easing.type: Easing.InOutCubic }
            }
        }
    }
    component FollowButton: Action {
        id: followButton
        property var user: ({})
        property bool compact: false
        readonly property string userUrn: user?.urn || ""
        readonly property bool followed: !!app.api.followings?.[userUrn]
        readonly property bool pending: !!app.api.followingPending?.[userUrn]
        visible: !!userUrn && userUrn !== (app.api.profile?.urn || "")
        enabled: !app.api.signedIn || (!app.api.followingsLoading && !pending)
        text: pending ? qsTr("Сохранение…") : app.api.followingsLoading ? qsTr("Загрузка…") : app.api.signedIn && !app.api.followingsReady ? qsTr("Повторить") : followed ? qsTr("Вы подписаны") : qsTr("Подписаться")
        Accessible.name: (followed ? qsTr("Отписаться от ") : qsTr("Подписаться на ")) + (user?.username || qsTr("исполнителя"))
        onUserUrnChanged: if (visible) Qt.callLater(sc.loadFollowings)
        Connections {
            target: sc
            function onSignedInChanged() { if (followButton.visible) sc.loadFollowings() }
        }
        Component.onCompleted: if (visible) Qt.callLater(sc.loadFollowings)
        onClicked: sc.toggleFollow(user || {})
        implicitHeight: 44
        primary: followed
        display: compact ? AbstractButton.IconOnly : AbstractButton.TextOnly
        symbol: compact ? (followed ? "following" : "follow") : ""
    }

    component Action: Button {
        id: control
        
        hoverEnabled: true
        property bool primary: false
        property string symbol: ""
        implicitHeight: 48
        leftPadding: 16
        rightPadding: 16
        font.pixelSize: 14
        font.bold: true
        Accessible.name: text
        scale: down && app.motion > 0 ? 0.965 : 1
        Behavior on scale { NumberAnimation { duration: app.motion * 0.5; easing.type: Easing.OutCubic } }
        contentItem: Item {
            implicitWidth: control.symbol ? 16 : label.implicitWidth
            implicitHeight: 22
            AndroidGlyph { anchors.centerIn: parent; visible: !!control.symbol; name: control.symbol; ink: control.primary ? app.foreground(app.accent) : app.ink; width: 22; height: 22 }
            Text { id: label; visible: !control.symbol; anchors.fill: parent; text: control.text; font: control.font; color: control.primary ? app.foreground(app.accent) : app.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        }
        background: Rectangle { radius: height / 2; color: control.down ? (control.primary ? Qt.darker(app.accent, 1.12) : (app.dark ? "#393939" : "#e7e3e7")) : control.primary ? app.accent : (app.dark ? "#2b2b2b" : "#eeeeee"); opacity: control.enabled ? 1 : 0.45; Behavior on color { ColorAnimation { duration: app.motion / 2 } } }
    }
    component Caption: MetadataLabel {
        color: app.ink; font.pixelSize: 15; elide: Text.ElideRight
        normalizeFallback: true
    }
    TrackSelection { id: trackSelection; rows: collectionTools.rows; context: (collectionTools.transformed ? collectionTools.sourceRows.length : "") + ":" + collectionTools.sortKey + ":" + collectionTools.descending + ":" + collectionTools.query + ":" + app.scrollKey + ":" + (app.api.profile?.urn || "") + ":" + sc.signedIn }
    component Track: ItemDelegate {
        
        id: row
        required property var modelData
        onPressAndHold: if (app.page !== 4 && !queued && selectionIndex >= 0) trackSelection.choose(selectionIndex)
        property int selectionIndex: -1
        SelectionInput { anchors.fill: parent; z: 20; mobile: true; visible: app.page !== 4 && eligible; enabled: app.page !== 4; selection: trackSelection; rowIndex: row.selectionIndex; host: app }
        property bool queued: false
        property int queueIndex: -1
        width: ListView.view.width
        height: 80
        leftPadding: 20
        rightPadding: 12
        background: Rectangle { x: 8; width: parent.width - 16; radius: 18; y: 4; height: parent.height - 8; color: row.down ? Qt.alpha(app.ink, 0.055) : "transparent"; Behavior on color { ColorAnimation { duration: prefs.motion ? 120 : 0 } } }
        contentItem: RowLayout {
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 54; Layout.preferredHeight: 54
                radius: app.page === 2 && app.searchKind === "users" ? width / 2 : app.coverRadius; color: (app.dark ? "#2c2c2c" : "#ededed"); clip: true
                RoundedImage { radius: app.coverRadius; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0; anchors.fill: parent; source: app.page === 2 && app.searchKind === "users" ? row.modelData.avatar_url || "" : app.cover(row.modelData); avatar: app.page === 2 && app.searchKind === "users" }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Caption { text: row.modelData.title || row.modelData.username || qsTr("Без названия"); Layout.fillWidth: true; font.bold: true; color: row.modelData.urn === app.api.current?.urn ? app.accent : app.ink }
                    AbstractButton {
                        visible: row.modelData.access === "preview"
                        Layout.preferredWidth: 18; Layout.preferredHeight: 18
                        hoverEnabled: true; focusPolicy: Qt.StrongFocus
                        Accessible.name: qsTr("Только фрагмент; полная версия может требовать Go+")
                        ToolTip.text: Accessible.name
                        ToolTip.visible: app.showTooltips && (hovered || down || visualFocus)
                        ToolTip.delay: 250
                        contentItem: Item {
                            Rectangle { anchors.centerIn: parent; width: 6; height: 6; radius: 3; color: app.accent }
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: Qt.alpha(app.accent, parent.down ? 0.20 : parent.hovered ? 0.14 : 0.07)
                            border.width: 1
                            border.color: Qt.alpha(app.accent, parent.visualFocus ? 0.75 : parent.hovered ? 0.32 : 0.16)
                        }
                    }
                }
                Caption { text: (sc.displayArtist(row.modelData) || (row.modelData.track_count ? row.modelData.track_count + qsTr(" треков") : "SoundCloud")) + (sc.trackAvailability(row.modelData) ? " · " + sc.trackAvailability(row.modelData) : ""); Layout.fillWidth: true; color: app.muted; font.pixelSize: 12; TapHandler { enabled: !!row.modelData.user?.urn && !sc.hasSeparateArtist(row.modelData); onTapped: app.openProfile(row.modelData.user) } }
            }
            EngagementAction { host: app; mobile: true; visible: (row.modelData.urn || "").startsWith("soundcloud:tracks:"); item: row.modelData }
            Action { text: qsTr("Действия"); symbol: "menu"; implicitWidth: 48; Accessible.name: qsTr("Действия с треком"); onClicked: { app.selectedTrack = row.modelData; app.selectedQueue = row.queued ? row.queueIndex : -1; trackMenu.open() } }
        }
        onClicked: {
            if (selectionIndex >= 0 && trackSelection.active) return
            if (queued) sc.playQueue(queueIndex)
            else if (app.page === 2 && app.searchKind === "users") app.openProfile(modelData)
            else if (((modelData.urn || "").startsWith("soundcloud:playlists:") || (modelData.urn || "").startsWith("soundcloud:system-playlists:")) || modelData.kind === "playlist") playlistView.begin(modelData)
            else if (selectionIndex >= 0) sc.playMix(collectionTools.rows, selectionIndex)
            else sc.play(modelData)
        }
    }

    header: Item {
        id: topBar
        height: 94 + app.headerTopInset
        Item {
            anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 16
            anchors.topMargin: app.headerTopInset
            ColumnLayout {
                anchors.left: parent.left; anchors.right: accountButton.left; anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                RowLayout {
                    spacing: 5
                    Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 22; radius: 7; color: app.accent; Image { anchors.fill: parent; anchors.margins: 3; source: "qrc:/qt/qml/SCrip/assets/blob.png"; fillMode: Image.PreserveAspectFit } }
                    Caption { text: "Sange"; color: app.muted; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1.4 }
                }
                Caption { text: [qsTr("Слушай своё"), qsTr("На твоей волне"), qsTr("Найди свой звук"), qsTr("Твоя коллекция"), qsTr("Профиль"), qsTr("Настройки"), qsTr("Плагины")][app.page]; font.pixelSize: 27; font.bold: true }
            }
            AbstractButton {
                
                id: accountButton
                width: 48; height: 48
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                Accessible.name: app.page === 4 ? qsTr("Настройки") : qsTr("Мой профиль")
                scale: down ? 0.94 : 1
                Behavior on scale { NumberAnimation { duration: app.motion / 2 } }
                contentItem: Item {
                    RoundedImage { radius: app.coverRadius; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0; anchors.centerIn: parent; width: 42; height: 42; avatar: true; source: app.api.profile?.avatar_url || ""; visible: app.page !== 4 }
                    AndroidGlyph { anchors.centerIn: parent; name: "settings"; visible: app.page === 4 }
                }
                background: Rectangle { radius: 24; color: accountButton.down ? (app.dark ? "#383838" : "#e7e7e7") : "transparent"; Behavior on color { ColorAnimation { duration: app.motion } } }
                onClicked: app.page === 4 ? app.page = 5 : app.openProfile(null)
            }
        }
    }

    ColumnLayout {
        id: body
        clip: true
        transform: Translate { id: pageOffset }
        anchors.fill: parent
        spacing: 0
        Rectangle {
            visible: !!app.api.error && !app.api.countryBlockError; Layout.fillWidth: true
            implicitHeight: errorText.implicitHeight + 20
            color: "#ffe8ee"
            MetadataLabel { id: errorText; anchors.fill: parent; anchors.margins: 10; text: app.api.error || ""; wrapMode: Text.Wrap; color: "#97233b" }
            TapHandler { onTapped: sc.clearError() }
        }
        NetworkAccess { host: app; mobile: true; Layout.fillWidth: true; Layout.margins: 12; visible: !!app.api.connectionCountryBlocked || !!app.api.connectionIssue }
        Action {
            visible: sc.signedIn && !app.api.connected && !!app.api.error
            text: app.api.busy ? qsTr("Подключение…") : qsTr("Повторить подключение")
            enabled: !app.api.busy
            Layout.fillWidth: true
            onClicked: sc.connectCatalog()
        }
        ColumnLayout {
            visible: !sc.signedIn
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.margins: 28; spacing: 20
            Item { Layout.fillHeight: true }
            Image { source: "qrc:/qt/qml/SCrip/assets/blob.png"; Layout.preferredWidth: 120; Layout.preferredHeight: 120; Layout.alignment: Qt.AlignHCenter; fillMode: Image.PreserveAspectFit; Rectangle { anchors.fill: parent; anchors.margins: -8; radius: 32; color: app.accent; z: -1 } }
            MetadataLabel { text: qsTr("Твоя музыка.\nВ твоём ритме."); font.pixelSize: 27; font.bold: true; color: app.ink; Layout.fillWidth: true; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            MetadataLabel { text: app.api.authBusy ? qsTr("Заверши вход в браузере и вернись в Sange") : qsTr("Любимые треки, новые находки и миксы SoundCloud"); color: app.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            Action { visible: app.api.disconnectStatus === "unconfirmed"; text: qsTr("Настройки SoundCloud"); Layout.fillWidth: true; onClicked: sc.openExternal("https://soundcloud.com/settings") }
            Action { text: app.api.authBusy ? qsTr("Отменить вход") : qsTr("Войти в SoundCloud"); primary: true; Layout.fillWidth: true; onClicked: app.api.authBusy ? sc.cancelLogin() : sc.login() }
            Item { Layout.fillHeight: true }
        }
        RowLayout {
            visible: sc.signedIn && app.page === 2
            Layout.fillWidth: true; Layout.margins: 20
            TextField {
                id: search
                Layout.fillWidth: true; implicitHeight: 48
                placeholderText: qsTr("Поиск или ссылка SoundCloud")
                color: app.ink; placeholderTextColor: (app.dark ? "#aaaaaa" : "#77717a"); selectionColor: app.accent; selectedTextColor: app.foreground(app.accent)
                font.pixelSize: 14; leftPadding: 14
                background: Rectangle { radius: 16; color: search.activeFocus ? (app.dark ? "#2c2c2c" : "#f2f2f2") : (app.dark ? "#242424" : "#eeeeee"); border.width: search.activeFocus ? 1 : 0; border.color: Qt.alpha(app.accent, 0.45); Behavior on color { ColorAnimation { duration: app.motion } } }
                onAccepted: app.searchNow()
            }
            Action { text: qsTr("Найти"); primary: true; onClicked: app.searchNow() }
            Action { text: qsTr("Сохранённые поиски"); symbol: "menu"; implicitWidth: 44; onClicked: collectionTools.openOptions(this) }
        }
        Flickable {
            visible: sc.signedIn && app.page === 2 && app.searchKind === "tracks"
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            Layout.preferredHeight: Math.min(contentHeight, app.height * 0.42)
            contentHeight: trackFilters.height; contentWidth: width; clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            TrackSearchFilters {
                id: trackFilters; width: parent.width
                host: app; backend: sc; mobile: true
                onSearchRequested: app.refresh()
            }
        }
        RowLayout {
            visible: sc.signedIn && (app.page === 2 || app.page === 3)
            Layout.leftMargin: 20; Layout.rightMargin: 20; Layout.bottomMargin: 12
            Repeater {
                model: app.page === 2 ? [qsTr("Треки"), qsTr("Плейлисты"), qsTr("Люди")] : [qsTr("Любимые"), qsTr("Плейлисты"), qsTr("История")]
                Action {
                    required property string modelData
                    required property int index
                    text: modelData; Layout.fillWidth: true
                    font.pixelSize: 12
                    primary: app.page === 2 ? app.searchKind === ["tracks", "sets", "users"][index] : (index === 0 ? !["playlists", "history"].includes(app.library) : app.library === ["likes", "playlists", "history"][index])
                    onClicked: {
                        if (app.page === 2) app.searchKind = ["tracks", "sets", "users"][index]
                        else app.library = ["likes", "playlists", "history"][index]
                        app.refresh()
                    }
                }
            }
        }
        Flickable {
            visible: sc.signedIn && app.page === 3 && !["playlists", "history"].includes(app.library)
            Layout.fillWidth: true; Layout.preferredHeight: 44; Layout.bottomMargin: 12
            contentWidth: libraryFilters.implicitWidth + 40; contentHeight: height; clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: libraryFilters
                x: 20; spacing: 8
                Repeater {
                    model: [{label: qsTr("Треки"), key: "likes"}, {label: qsTr("Плейлисты"), key: "likedPlaylists"}, {label: qsTr("Станции"), key: "likedStations"}]
                    Action { required property var modelData; text: modelData.label; primary: app.library === modelData.key; font.pixelSize: 12; implicitHeight: 44; onClicked: { app.library = modelData.key; app.refresh() } }
                }
            }
        }
        Action {
            visible: sc.signedIn && app.page === 3 && app.library === "playlists"
            text: qsTr("Создать плейлист"); Layout.leftMargin: 20; Layout.bottomMargin: 12
            onClicked: createPlaylist.open()
        }
        ScrollView {
            visible: sc.signedIn && app.page === 0
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            Column {
                width: parent.width; spacing: 24
                WavePanel {
                    showTooltips: app.showTooltips
                    x: 18; width: parent.width - 36
                    backend: sc
                    dark: app.dark
                    panelColor: app.panel
                    inkColor: app.ink
                    waveColor: app.accent
                    motion: prefs.motion
                    exposure: player.visible ? Math.max(0, Math.min(1, player.y / Math.max(1, player.height))) : 1
                    foreground: app.visible && app.page === 0 && !queue.visible && !playlistView.visible
                        && app.visibility !== Window.Minimized
                        && (Qt.platform.os !== "android" || Qt.application.state === Qt.ApplicationActive)
                }
                Repeater {
                    id: homeShelves
                    model: shelves.length
                    readonly property var shelves: {
                        const shelves = sc.homeShelves || []
                        const artists = new Set()
                        const seeds = []
                        for (const shelf of shelves) for (const track of shelf.tracks || []) {
                            const id = track.user?.urn
                            if (id && !artists.has(id) && seeds.length < 16) { artists.add(id); seeds.push(track) }
                        }
                        return (seeds.length ? [{title: qsTr("Станции исполнителей"), station: true, tracks: seeds}] : []).concat(shelves)
                    }
                    Column {
                        id: homeShelf
                        required property int index
                        readonly property var modelData: homeShelves.shelves[index] || ({tracks: []})
                        width: parent.width; spacing: 10
                        MetadataLabel { x: 18; width: parent.width - 36; text: parent.modelData.title; color: app.ink; font.pixelSize: 19; font.bold: true; wrapMode: Text.Wrap; TapHandler { enabled: !homeShelf.modelData.station; onTapped: playlistView.beginMix(homeShelf.modelData, shelf.songs) } }
                        Caption { x: 18; width: parent.width - 36; visible: !!homeShelf.modelData.subtitle; text: homeShelf.modelData.subtitle || ""; color: app.muted; font.pixelSize: 12 }
                        ListView {
                            id: shelf
                            property var songs: parent.modelData.tracks || []
                            width: parent.width; height: 218; orientation: ListView.Horizontal
                            flickableDirection: Flickable.HorizontalFlick
                            leftMargin: 20; rightMargin: 20; spacing: 14; clip: true
                            model: songs.length
                            delegate: ItemDelegate {
                                
                                required property int index
                                readonly property var modelData: shelf.songs[index] || ({})
                                objectName: homeShelf.modelData.station ? "artistStationCard" + index : "homeTrackCard" + index
                                width: 154; height: 218; padding: 0
                                background: Item {}
                                contentItem: Column {
                                    spacing: 7
                                    Rectangle {
                                        width: 154; height: 154; radius: app.coverRadius; color: (app.dark ? "#2c2c2c" : "#ededed"); clip: true
                                        RoundedImage { radius: app.coverRadius; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0; anchors.fill: parent; source: homeShelf.modelData.station ? modelData.user?.avatar_url || app.cover(modelData) : app.cover(modelData) }
                                    }
                                    Caption { width: 154; text: homeShelf.modelData.station ? sc.displayArtist(modelData) || qsTr("Исполнитель") : modelData.title || ""; font.bold: true; font.pixelSize: 13 }
                                    Caption { width: 154; text: homeShelf.modelData.station ? (modelData.user?.station_urn ? qsTr("Станция исполнителя") : qsTr("Подборка Sange")) : sc.displayArtist(modelData) || ""; color: app.muted; font.pixelSize: 12 }
                                }
                                onClicked: {
                                    if (homeShelf.modelData.station) playlistView.beginStation(modelData)
                                    else if (((modelData.urn || "").startsWith("soundcloud:playlists:") || (modelData.urn || "").startsWith("soundcloud:system-playlists:")) || modelData.kind === "playlist") playlistView.begin(modelData)
                                    else playlistView.beginMix(homeShelf.modelData, shelf.songs)
                                }
                            }
                        }
                    }
                }
                MetadataLabel { x: 18; width: parent.width - 36; visible: !(sc.homeShelves || []).length; text: app.api.homeBusy || (!app.homeAccount && !app.api.error) ? qsTr("Собираем твою музыку…") : app.api.homeError || app.api.error || qsTr("Добавь любимые треки, чтобы здесь появились подборки"); wrapMode: Text.Wrap; color: app.muted }
                PluginPanels { dark: app.dark; ink: app.ink; muted: app.muted; x: 18; width: parent.width - 36; manager: pluginManager; slotName: "home"; onReport: error => app.message = error }
                Item { width: 1; height: navigationFooter.height + 16 }
            }
        }
        ScrollView {
            id: settingsScroll
            visible: app.page === 5 || app.page === 6
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: 18
                Action { text: qsTr("Назад"); symbol: "back"; Layout.leftMargin: 20; onClicked: app.page = app.page === 6 ? 5 : 4 }
                NetworkAccess { host: app; mobile: true; visible: app.page === 5; Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18 }
                ColumnLayout {
                    visible: app.page === 5
                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.margins: 18; spacing: 14
                    Caption { text: qsTr("Язык интерфейса"); Layout.fillWidth: true; wrapMode: Text.Wrap; font.bold: true; font.pixelSize: 19 }
                    ComboBox {
                        objectName: "interfaceLanguageControl"
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        model: ["Русский", "English"]
                        currentIndex: uiLanguage.language === "ru" ? 0 : 1
                        Accessible.name: qsTr("Язык интерфейса")
                        onActivated: index => uiLanguage.language = index === 0 ? "ru" : "en"
                    }
                    Caption { text: "Sange"; font.bold: true; font.pixelSize: 19 }
                    Caption { text: qsTr("Версия ") + Qt.application.version + (app.api.updateAvailable ? qsTr(" · доступна ") + app.api.latestVersion : ""); color: app.api.updateAvailable ? app.accent : app.muted; font.pixelSize: 13 }
                    Caption { text: app.api.updateError || app.api.updateResult || (app.api.updateChecking ? qsTr("Проверяем обновления…") : qsTr("Проверка ещё не выполнялась")); color: app.api.updateError ? "#a22c39" : app.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Caption { visible: !!app.api.updateDownloading; text: app.api.updateTotal > 0 ? (Number(app.api.updateDownloaded / 1048576).toFixed(1) + qsTr(" МБ / ") + Number(app.api.updateTotal / 1048576).toFixed(1) + qsTr(" МБ · ") + Math.floor(100 * app.api.updateDownloaded / app.api.updateTotal) + "%") : (app.api.updateDownloaded + qsTr(" байт скачано")); color: app.muted; Layout.fillWidth: true }
                    ProgressBar { visible: !!app.api.updateDownloading; Layout.fillWidth: true; indeterminate: !(app.api.updateTotal > 0); from: 0; to: 100; value: app.api.updateTotal > 0 ? 100 * app.api.updateDownloaded / app.api.updateTotal : 0 }
                    Action { text: qsTr("Проверить обновления"); visible: !app.api.updateChecking && !app.api.updateDownloading && !app.api.updateInstalling && !app.api.updateReady; enabled: !app.api.updateChecking; Layout.fillWidth: true; onClicked: sc.checkForUpdates(true) }
                    Action { text: qsTr("Скачать обновление"); visible: !!app.api.updateAvailable && !app.api.updateDownloading && !app.api.updateReady && !app.api.updateInstalling; Layout.fillWidth: true; onClicked: sc.downloadUpdate() }
                    Action { text: qsTr("Отмена"); visible: !!app.api.updateDownloading; Layout.fillWidth: true; onClicked: sc.cancelUpdateDownload() }
                    Action { text: qsTr("Установить сейчас"); visible: !!app.api.updateReady && !app.api.updateInstalling; Layout.fillWidth: true; onClicked: sc.installUpdate() }
                    Caption { text: qsTr("Воспроизведение"); font.bold: true; font.pixelSize: 19 }

                    Action { text: qsTr("Исключения рекомендаций · ") + (app.api.recommendationExclusions || []).length; Layout.fillWidth: true; onClicked: recommendationSettings.open() }
                    Caption { text: qsTr("Качество воспроизведения"); Layout.fillWidth: true; wrapMode: Text.Wrap }
                    PlaybackQuality {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: 360; Layout.alignment: Qt.AlignLeft
                        objectName: "playbackQualityControl"
                        value: app.api.playbackQuality || 0; onEdited: value => sc.setPlaybackQuality(value)
                        ink: app.ink; surface: app.raised; accent: app.accent; selectedInk: app.foreground(app.accent); motion: app.motion
                    }
                    Switch { Layout.fillWidth: true; Layout.minimumWidth: 0; objectName: "preloadControl"; Accessible.name: qsTr("Предзагрузка"); text: qsTr("Предзагрузка"); checked: !!app.api.preloadEnabled; onToggled: sc.setPreloadEnabled(checked) }
                    Caption { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Заранее загружать следующий трек и сегменты текущего. Качество применяется со следующей загрузки."); color: app.muted }
                    Switch { visible: !!app.api.wifiPolicyAvailable || !!app.api.preloadWifiOnly; enabled: !!app.api.preloadEnabled; objectName: "preloadWifiControl"; text: qsTr("Только по Wi-Fi"); Accessible.name: qsTr("Предзагрузка только по Wi-Fi"); checked: !!app.api.preloadWifiOnly; onToggled: sc.setPreloadWifiOnly(checked) }
                    Caption { visible: !!app.api.preloadWifiOnly; Layout.fillWidth: true; wrapMode: Text.Wrap; color: app.muted; text: qsTr("Если тип сети неизвестен, предзагрузка отключена.") }
                    Caption { text: qsTr("Оформление"); font.bold: true; font.pixelSize: 19 }
                    Switch {  text: qsTr("Тёмная тема"); checked: prefs.darkMode; onToggled: app.switchTheme(checked) }
                    Row {
                        spacing: 14
                        Repeater {
                            model: ["#f43756", "#7554ba", "#238579", "#2777c7", "#242426"]
                            AbstractButton {
                                
                                required property string modelData
                                width: 42; height: 42; Accessible.name: qsTr("Цвет ") + modelData
                                background: Rectangle { color: modelData; radius: 21; border.width: prefs.accentOverride && prefs.accent === modelData ? 4 : 0; border.color: (app.dark ? "#e7e7e7" : "#d0d0d0") }
                                onClicked: { prefs.accent = modelData; prefs.accentOverride = true }
                            }
                        }
                    }
                    Caption { text: qsTr("Скругление обложек"); color: app.muted }
                    FineSlider { Layout.fillWidth: true; from: 0; to: 28; value: prefs.coverRadius; onMoved: { prefs.coverRadius = value; prefs.coverRadiusOverride = true } Accessible.name: qsTr("Скругление обложек") }
                    Action { Layout.minimumWidth: 0; Layout.maximumWidth: parent.width; text: qsTr("Использовать оформление плагинов"); onClicked: { prefs.accentOverride = false; prefs.coverRadiusOverride = false } }
                    Switch { text: qsTr("Всплывающие подсказки"); checked: prefs.showTooltips; onToggled: prefs.showTooltips = checked }
                    Switch {  text: qsTr("Анимации"); checked: prefs.motion; onToggled: prefs.motion = checked }
                    Switch { Layout.minimumWidth: 0; Layout.maximumWidth: parent.width; text: qsTr("Догружать при прокрутке"); checked: prefs.autoLoad; onToggled: prefs.autoLoad = checked }
                    MetadataLabel { text: app.message; visible: !!text && app.page === 5; wrapMode: Text.Wrap; color: app.muted; Layout.fillWidth: true }
                    Caption { text: qsTr("Аккаунт"); font.bold: true; font.pixelSize: 19 }
                    Action { text: sc.signedIn ? qsTr("Выйти из SoundCloud") : qsTr("Войти в SoundCloud"); Layout.fillWidth: true; onClicked: sc.signedIn ? sc.logout() : sc.login() }
                    DiagnosticsPanel {
                        host: app; mobile: true
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        ink: app.ink; muted: app.muted
                    }
                    Action { text: qsTr("Плагины"); Layout.fillWidth: true; onClicked: app.page = 6 }
                    Caption { text: "Sange " + Qt.application.version + " · Android"; color: app.muted; font.pixelSize: 12 }
                }
                ColumnLayout {
                    visible: app.page === 6
                    Layout.fillWidth: true; Layout.margins: 18
                    PluginManagerView { dark: app.dark; ink: app.ink; muted: app.muted; Layout.fillWidth: true; manager: pluginManager; appearanceHint: prefs.accentOverride || prefs.coverRadiusOverride ? qsTr("Пользовательское оформление перекрывает соответствующие свойства темы плагина.") : "" }
                }
                Item { Layout.fillWidth: true; implicitHeight: navigationFooter.height }
            }
        }
        CollectionTools {
            id: collectionTools; objectName: "collectionTools"; host: app; backend: sc; mobile: true
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            visible: sc.signedIn && app.page > 0 && app.page < 4 && app.page !== 2
            catalogue: app.page === 2
            contextKey: app.scrollKey
            catalogueQuery: search.text
            catalogueKind: app.searchKind
            filters: app.searchKind === "tracks" ? trackFilters.applied : ({})
            onPresetApplied: (query, filters) => { if (catalogue) { search.text = query; if (app.searchKind === "tracks") trackFilters.restore(filters); else app.refresh() } }
        }
        SelectionBar { id: selectionBar; objectName: "collectionSelectionBar"; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; host: app; mobile: true; selection: trackSelection; visible: sc.signedIn && app.page > 0 && app.page < 4 && !(app.page === 2 && app.searchKind === "users") }
        ListView {
            id: tracks
            move: Transition { NumberAnimation { properties: "x,y"; duration: app.motion; easing.type: Easing.OutCubic } }
            displaced: Transition { NumberAnimation { properties: "x,y"; duration: app.motion; easing.type: Easing.OutCubic } }
            bottomMargin: navigationFooter.height
            visible: sc.signedIn && app.page > 0 && app.page < 5
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; model: collectionTools.transformed ? collectionTools.rows : sc.trackModel
            reuseItems: true
            cacheBuffer: Math.max(320, height / 2)
            pixelAligned: false
            function maybeLoad() {
                if (visible && !collectionTools.query.trim() && prefs.autoLoad && app.api.hasMore && !app.api.busy && !app.api.error
                        && contentY + height >= originY + contentHeight + bottomMargin - 180) sc.more()
            }
            readonly property bool paginationReady: visible && !collectionTools.query.trim() && prefs.autoLoad && app.api.hasMore && !app.api.busy && !app.api.error
            onPaginationReadyChanged: loadTimer.restart()
            onContentHeightChanged: loadTimer.restart()
            onHeightChanged: loadTimer.restart()
            Timer { id: loadTimer; interval: 100; onTriggered: tracks.maybeLoad() }
            onCountChanged: if (app.page === 4 && app.profileTabResetPending) positionViewAtBeginning()
            onContentYChanged: {
                if (!app.restoringScroll && app.activeScrollKey === app.scrollKey) app.scrollPositions[app.activeScrollKey] = contentY
                if (!loadTimer.running) loadTimer.start()
            }
            header: Column {
                width: tracks.width
                visible: app.page === 4
                height: visible ? implicitHeight : 0
                spacing: 14
                Rectangle {
                    id: profileCard
                    x: 16; width: parent.width - 32
                    height: profileContent.implicitHeight + 30; radius: 22
                    color: (app.dark ? "#2c2c2c" : "#f2f2f2"); clip: true
                    readonly property bool creator: app.profile?.urn === "soundcloud:users:1675018934"
                    readonly property var visuals: app.profile?.visuals?.visuals || app.profile?.visuals || []
                    readonly property string banner: app.profile?.banner_url || app.profile?.cover_url || (visuals.length ? visuals[0]?.visual_url || "" : "")
                    Column {
                        id: profileContent
                        width: parent.width; spacing: 12
                        Item {
                            width: parent.width; height: 158
                            Rectangle { anchors.fill: parent; color: (app.dark ? "#2c2c2c" : "#ededed") }
                            Image {
                                anchors.fill: parent; source: profileCard.banner
                                visible: status === Image.Ready; asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                            }
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 10
                                width: 86; height: 86; radius: 43
                                color: app.surface; border.width: profileCard.creator ? 3 : 2
                                border.color: profileCard.creator ? app.accent : app.surface
                                RoundedImage { anchors.fill: parent; anchors.margins: 4; avatar: true; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0; source: app.profile?.avatar_url || "" }
                            }
                            Rectangle {
                                visible: profileCard.creator
                                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                                width: creatorBadge.implicitWidth + 20; height: 27; radius: 14; color: app.accent
                                Text { id: creatorBadge; anchors.centerIn: parent; text: qsTr("✦ Создатель Sange"); color: app.foreground(app.accent); font.pixelSize: 11; font.bold: true }
                            }
                        }
                        FollowButton { anchors.horizontalCenter: parent.horizontalCenter; user: app.profile || ({}) }
                        Caption { anchors.horizontalCenter: parent.horizontalCenter; width: parent.width - 32; horizontalAlignment: Text.AlignHCenter; text: app.profile?.username || qsTr("Твой профиль"); font.pixelSize: 19; font.bold: true }
                        Caption {
                            anchors.horizontalCenter: parent.horizontalCenter; width: parent.width - 32; horizontalAlignment: Text.AlignHCenter
                            color: app.accent; font.bold: true; font.pixelSize: 12; visible: !!text
                            text: {
                                const p = app.profile || {}
                                const products = (p.subscriptions || []).map(s => (s.product?.id || "") + " " + (s.product?.name || "")).join(" ")
                                const info = (products + " " + (p.plan || "")).toLowerCase()
                                const badges = []
                                if (/artist[ -]pro|pro[ -]unlimited|pro plus/.test(info)) badges.push("Artist Pro")
                                if (/go[ +_-]plus|go\+/.test(info)) badges.push("Go+")
                                else if (/\bgo\b/.test(info)) badges.push("Go")
                                return badges.join(" · ")
                            }
                        }
                        Caption { anchors.horizontalCenter: parent.horizontalCenter; width: parent.width - 32; horizontalAlignment: Text.AlignHCenter; visible: !!text; text: [app.profile?.city, app.profile?.country].filter(Boolean).join(", "); color: app.muted; font.pixelSize: 12 }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter; spacing: 22
                            Repeater {
                                model: [{value: app.profile?.followers_count || 0, label: qsTr("подписчиков")}, {value: app.profile?.followings_count || 0, label: qsTr("подписок")}, {value: app.profile?.track_count || 0, label: qsTr("треков")}]
                                ProfileStat {
                                    required property var modelData
                                    required property int index
                                    host: app; value: modelData.value; caption: modelData.label
                                    onClicked: { if (index < 2) peoplePopup.begin(app.profile, index === 0 ? "followers" : "followings"); else { app.profileTab = 0; app.refresh() } }
                                }
                            }
                        }
                    }
                }
                ProfileExtras { host: app; mobile: true; user: app.profile || ({}); x: 18; width: parent.width - 36; onRelatedRequested: peoplePopup.begin(app.profile, "related") }
                MetadataLabel { x: 18; width: parent.width - 36; text: app.profile?.description || ""; visible: !!text; wrapMode: Text.Wrap; color: app.muted; maximumLineCount: 4; elide: Text.ElideRight }
                ProfileNavigation {
                    x: 18; width: parent.width - 36
                    host: app; mobile: true; section: app.profileTab; likes: app.profileLikes; reposts: app.repostLibrary
                    onNavigate: (section, collection) => {
                        if (section === 2) app.profileLikes = collection
                        if (section === 3) app.repostLibrary = collection
                        app.profileTabResetPending = true; app.profileTab = section; app.refresh()
                        if (prefs.motion) pageArrival.restart()
                        Qt.callLater(function() { tracks.positionViewAtBeginning() })
                    }
                }
                PluginPanels { dark: app.dark; ink: app.ink; muted: app.muted; x: 18; width: parent.width - 36; manager: pluginManager; slotName: "profile"; onReport: error => app.message = error }
                Item { height: 8; width: 1 }
            }
            delegate: Track { required property int index; selectionIndex: index }
            footer: Column {
                width: tracks.width
                spacing: 16
                MetadataLabel {
                    width: parent.width - 48; x: 24
                    topPadding: 24; bottomPadding: 24
                    horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                    visible: !tracks.count; color: app.muted
                    text: collectionTools.query.trim() ? (app.api.hasMore || app.api.busy || app.api.collectionSearchStatus === "error" || app.api.collectionSearchStatus === "limit" ? qsTr("В загруженной части совпадений нет") : qsTr("Ничего не найдено")) : app.api.busy ? qsTr("Загрузка…") : app.api.collectionStatus === "unavailable" ? qsTr("Раздел недоступен") : app.api.collectionStatus === "error" ? qsTr("Не удалось загрузить раздел") : app.page === 4 ? qsTr("В этом разделе пока пусто") : app.page === 2 ? qsTr("Найди то, что хочется услышать") : app.page === 4 && app.profileTab === 1 ? qsTr("Здесь пока нет плейлистов") : qsTr("Здесь пока нет треков")
                }
                Action { anchors.horizontalCenter: parent.horizontalCenter; visible: !!app.api.hasMore; text: qsTr("Показать ещё"); enabled: !app.api.busy; onClicked: sc.more() }
            }
        }
    }

    Connections {
        target: sc
        function onCatalogChanged() {
            if (app.profileTabResetPending && app.page === 4 && !app.api.busy) {
                Qt.callLater(function() {
                    if (app.profileTabResetPending && app.page === 4) {
                        tracks.positionViewAtBeginning()
                        app.profileTabResetPending = false
                    }
                })
            }
        }
    }

    Item {
        id: navigationFooter
        objectName: "navigationColumn"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        z: 10
        height: implicitHeight
        implicitHeight: navigationStack.implicitHeight
        Column {
        id: navigationStack
        width: parent.width
        spacing: 10
        AbstractButton {
            id: miniPlayer
            objectName: "playbackDock"
            visible: !!app.api.current?.title
            x: 12; width: parent.width - 24; height: 72
            Accessible.name: qsTr("Открыть плеер: ") + (app.api.current?.title || "")
            onClicked: player.open()
            background: Rectangle {
                radius: 22
                color: miniPlayer.down ? app.raised : app.panel
                border.width: 1; border.color: Qt.alpha(app.ink, 0.07)
                Behavior on color { ColorAnimation { duration: prefs.motion ? 120 : 0 } }
            }
            DragHandler {
                id: miniSwipe
                property real releaseSpeed: 0
                property double lastMoveTime: 0
                onActiveTranslationChanged: if (active) {
                    releaseSpeed = centroid.velocity.y
                    lastMoveTime = Date.now()
                }
                target: null
                xAxis.enabled: false
                property real startY: 0
                onActiveChanged: {
                    if (active) {
                        if (centroid.scenePosition.y >= centroid.scenePressPosition.y) return
                        startY = centroid.scenePressPosition.y
                        player.manualMotion = true
                        player.dragging = true
                        playerMotion.stop()
                        playerMotion.value = player.height
                        player.open()
                        player.opacity = 1
                        player.reveal = 1
                        player.dragOffset = playerMotion.value
                    } else if (player.manualMotion) {
                        player.dragging = false
                        playerMotion.settle(Date.now() - lastMoveTime < 100 ? releaseSpeed : 0, player.height - Math.min(100, player.height * 0.25))
                    }
                }
                onCentroidChanged: if (active && player.dragging && player.manualMotion) {
                    playerMotion.value = Math.max(0, Math.min(player.height,
                        player.height + centroid.scenePosition.y - startY))
                }
            }
            RowLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 10
                RoundedImage {
                    Layout.preferredWidth: 48; Layout.preferredHeight: 48
                    source: app.cover(app.api.current); radius: 14
                    placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    Caption { text: app.api.current?.title || ""; Layout.fillWidth: true; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Caption { text: sc.displayArtist(app.api.current || {}) || ""; Layout.fillWidth: true; color: app.muted; font.pixelSize: 11 }
                    Rectangle {
                        Layout.fillWidth: true; Layout.topMargin: 3
                        height: 3; radius: 1.5; color: Qt.alpha(app.ink, 0.10)
                        Rectangle {
                            height: parent.height; radius: parent.radius
                            width: parent.width * Math.max(0, Math.min(1, (sc.position || 0) / Math.max(1, sc.duration || 0)))
                            color: app.ink; opacity: 0.65
                        }
                    }
                }
                AbstractButton {
                    id: miniPlay
                    Layout.preferredWidth: 44; Layout.preferredHeight: 44
                    Accessible.name: app.api.playing ? qsTr("Пауза") : qsTr("Воспроизвести")
                    onClicked: sc.togglePlayback()
                    scale: down ? 0.94 : 1
                    Behavior on scale { NumberAnimation { duration: prefs.motion ? 120 : 0 } }
                    background: Rectangle { radius: 22; color: miniPlay.down ? Qt.alpha(app.ink, 0.8) : app.ink }
                    contentItem: Item {
                        AndroidGlyph { anchors.centerIn: parent; width: 20; height: 20; name: app.api.playing ? "pause" : "play"; ink: app.surface; visible: !app.api.buffering }
                        BusyIndicator { anchors.centerIn: parent; width: 28; height: 28; palette.dark: app.surface; running: !!app.api.buffering; visible: running }
                    }
                }
                FollowButton { user: app.api.current?.user || ({}); compact: true; implicitWidth: 44 }
                EngagementAction { host: app; mobile: true; item: app.api.current || ({}) }
            }
        }
        Item {
            width: parent.width
            height: 78
            RowLayout {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 12; anchors.rightMargin: 12
                height: 62; spacing: 10
                Rectangle {
                    id: navigationCapsule
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: height / 2
                    color: app.panel
                    border.width: 1
                    border.color: (app.dark ? "#242424" : "#eeeeee")
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true; shadowColor: "#180f1724"
                        shadowBlur: 0.8; shadowVerticalOffset: 4
                    }
                    readonly property int selected: app.page === 0 ? 0 : app.page === 1 ? 1 : app.page === 3 ? 2 : -1
                    Rectangle {
                        x: 5 + Math.max(0, navigationCapsule.selected) * (navigationCapsule.width - 10) / 3
                        y: 5
                        width: (navigationCapsule.width - 10) / 3; height: parent.height - 10
                        radius: height / 2
                        color: (app.dark ? "#383838" : "#f0f0f0")
                        opacity: navigationCapsule.selected >= 0 ? 1 : 0
                        Behavior on x { NumberAnimation { duration: app.motion; easing.type: Easing.OutQuint } }
                        Behavior on opacity { NumberAnimation { duration: app.motion / 2 } }
                    }
                    Row {
                        anchors.fill: parent; anchors.margins: 5
                        Repeater {
                            model: [
                                { title: qsTr("Главная"), symbol: "home", page: 0 },
                                { title: qsTr("Лента"), symbol: "list", page: 1 },
                                { title: qsTr("Медиатека"), symbol: "music", page: 3 }
                            ]
                            AbstractButton {
                                id: navigationTab
                                required property var modelData
                                required property int index
                                width: parent.width / 3; height: parent.height
                                topPadding: 5; bottomPadding: 5
                                Accessible.name: modelData.title
                                readonly property bool selected: app.page === modelData.page
                                scale: down ? 0.96 : 1
                                Behavior on scale { NumberAnimation { duration: app.motion * 0.5; easing.type: Easing.OutCubic } }
                                contentItem: Column {
                                    spacing: 3
                                    AndroidGlyph {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 22; height: 22
                                        name: navigationTab.modelData.symbol
                                        ink: navigationTab.selected ? app.accent : app.ink
                                        Behavior on ink { ColorAnimation { duration: app.motion / 2 } }
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: navigationTab.modelData.title
                                        font.pixelSize: 11; font.bold: navigationTab.selected
                                        color: navigationTab.selected ? app.accent : app.ink
                                        Behavior on color { ColorAnimation { duration: app.motion / 2 } }
                                    }
                                }
                                onClicked: app.page = modelData.page
                            }
                        }
                    }
                }
                AbstractButton {
                    id: floatingSearch
                    Layout.preferredWidth: 62; Layout.preferredHeight: 62
                    Accessible.name: qsTr("Поиск")
                    scale: down ? 0.95 : 1
                    Behavior on scale { NumberAnimation { duration: app.motion * 0.5; easing.type: Easing.OutCubic } }
                    background: Rectangle {
                        radius: width / 2
                        color: floatingSearch.down ? Qt.darker(app.accent, 1.1) : app.accent
                        Behavior on color { ColorAnimation { duration: app.motion / 2 } }
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true; shadowColor: Qt.alpha(app.accent, 0.23)
                            shadowBlur: 0.7; shadowVerticalOffset: 4
                        }
                    }
                    contentItem: Item {
                        AndroidGlyph { anchors.centerIn: parent; name: "search"; width: 25; height: 25; ink: app.foreground(app.accent) }
                    }
                    onClicked: { app.page = 2; search.forceActiveFocus() }
                }
            }
        }
        Item { width: 1; height: app.safeAreaBottom(navigationFooter) }
        }
    }

    Sheet {
        id: player
        swipeDismissEnabled: false
        objectName: "playerSheet"
        // Move the popup itself: its surface, content and hit area share one
        // position. Moving content alone leaves Qt's full-screen popup behind.
        unifiedSurface: true
        y: (1 - reveal) * height + dragOffset
        dim: false
        parent: Overlay.overlay
        width: parent.width; height: parent.height
        modal: !miniSwipe.active; focus: true; closePolicy: Popup.CloseOnEscape; padding: 26
        topPadding: 12 + app.safeAreaTop(parent)
        bottomPadding: 16 + app.safeAreaBottom(parent)
        background: Rectangle {
            gradient: Gradient {
                GradientStop { position: 0; color: app.panel }
                GradientStop { position: 0.65; color: app.surface }
            }
        }
        SheetMotion {
            id: playerMotion
            extent: player.height
            onValueChanged: if (player.manualMotion) player.dragOffset = value
            onSettled: closed => {
                if (closed) player.close()
                else player.manualMotion = false
            }
        }
        onAboutToHide: playerMotion.stop()
        onClosed: {
            playerMotion.stop()
            dragOffset = 0
            manualMotion = false
        }
        PointHandler {
            enabled: !miniSwipe.active
            acceptedButtons: Qt.LeftButton
            onActiveChanged: {
                if (active) {
                    playerMotion.stop()
                    playerMotion.value = player.dragOffset
                } else if (!playerSwipe.active && player.manualMotion && !playerMotion.running) {
                    playerMotion.settle(0)
                }
            }
        }
        DragHandler {
            id: playerSwipe
            grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.ApprovesTakeOverByAnything
            property real releaseSpeed: 0
            property double lastMoveTime: 0
            onActiveTranslationChanged: if (active) {
                releaseSpeed = centroid.velocity.y
                lastMoveTime = Date.now()
            }
            target: null
            xAxis.enabled: false
            property real startOffset: 0
            property real startY: 0
            onActiveChanged: {
                if (active) {
                    playerMotion.stop()
                    startOffset = player.dragOffset
                    startY = centroid.scenePosition.y
                    player.manualMotion = true
                    player.dragging = true
                } else {
                    player.dragging = false
                    playerMotion.settle(Date.now() - lastMoveTime < 100 ? releaseSpeed : 0)
                }
            }
            onCentroidChanged: if (active) playerMotion.value = Math.max(0,
                Math.min(player.height, startOffset + centroid.scenePosition.y - startY))
        }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                id: playerHeader
                Layout.fillWidth: true
                Action { text: qsTr("Свернуть"); symbol: "back"; Layout.preferredWidth: 48; Accessible.name: qsTr("Свернуть плеер"); onClicked: player.close() }
                Action { text: qsTr("Дополнения плеера"); symbol: "grid"; Layout.preferredWidth: 48; visible: pluginManager.panels.some(p => p.page === "player"); onClicked: pluginTools.open() }
                Caption { text: qsTr("СЕЙЧАС ИГРАЕТ"); font.pixelSize: 11; font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                Action { text: qsTr("Действия"); symbol: "menu"; Layout.preferredWidth: 48; Accessible.name: qsTr("Действия с треком"); onClicked: { app.selectedTrack = app.api.current; app.selectedQueue = -1; trackMenu.open() } }
            }
            NetworkAccess {
                id: playerCountryAccess
                host: app; mobile: true; Layout.fillWidth: true
                visible: !!app.api.connectionCountryBlocked && !app.api.relayEnabled
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                id: playerCover
                objectName: "playerCover"
                Layout.preferredWidth: Math.max(48, Math.min(player.availableWidth,
                    player.availableHeight - playerHeader.implicitHeight - playerDetails.implicitHeight
                    - (playerCountryAccess.visible ? playerCountryAccess.implicitHeight + 10 : 0) - 20))
                Layout.preferredHeight: Layout.preferredWidth
                Layout.minimumWidth: 0; Layout.minimumHeight: 0
                Layout.maximumWidth: Layout.preferredWidth
                Layout.maximumHeight: Layout.preferredHeight
                color: (app.dark ? "#2c2c2c" : "#e9e9e9"); radius: 24; clip: true
                RoundedImage { anchors.fill: parent; source: app.cover(app.api.current); radius: 24; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0 }
            }
            ColumnLayout {
                id: playerDetails
                objectName: "playerDetails"
                Layout.fillWidth: true
                spacing: 10
                AbstractButton {
                    id: commentPreview
                    objectName: "playingCommentPreview"
                    Layout.fillWidth: true; Layout.preferredHeight: 64
                    leftPadding: 14; rightPadding: 14; topPadding: 10; bottomPadding: 10
                    property var displayedComment: ({})
                    readonly property string desiredKey: String(app.api.current.urn || "") + ":" + (app.playingComment.urn || "")
                    onDesiredKeyChanged: { commentArrival.stop(); commentArrival.start() }
                    Accessible.name: displayedComment.body ? qsTr("Комментарий: ") + displayedComment.body + qsTr(". Открыть комментарии") : qsTr("Открыть комментарии к треку")
                    onClicked: { commentsSheet.followCurrent = true; commentsSheet.open() }
                    background: Rectangle {
                        radius: 16
                        color: Qt.alpha(app.ink, commentPreview.down ? 0.09 : 0.045)
                        border.width: 1; border.color: Qt.alpha(app.ink, 0.035)
                    }
                    SequentialAnimation {
                        id: commentArrival
                        NumberAnimation { target: previewContent; property: "opacity"; to: 0; duration: app.motion * 0.3; easing.type: Easing.InQuad }
                        ScriptAction { script: commentPreview.displayedComment = app.playingComment }
                        ParallelAnimation {
                            NumberAnimation { target: previewContent; property: "opacity"; to: 1; duration: app.motion * 0.65; easing.type: Easing.OutCubic }
                            NumberAnimation { target: previewOffset; property: "y"; from: 4; to: 0; duration: app.motion * 0.65; easing.type: Easing.OutCubic }
                        }
                    }
                    contentItem: RowLayout {
                        id: previewContent
                        spacing: 12
                        transform: Translate { id: previewOffset }
                        Caption {
                            Layout.preferredWidth: 38; Layout.alignment: Qt.AlignVCenter
                            text: commentPreview.displayedComment.body ? app.time(commentPreview.displayedComment.timestamp) : "···"
                            font.pixelSize: 11; font.features: { "tnum": 1 }; color: app.muted
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Qt.alpha(app.ink, 0.1) }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 2
                            Caption { Layout.fillWidth: true; text: commentPreview.displayedComment.user?.username || qsTr("Комментарии"); font.pixelSize: 11; color: app.muted }
                            Caption { Layout.fillWidth: true; text: commentPreview.displayedComment.body || qsTr("Обсудить этот трек"); textFormat: Text.PlainText; font.pixelSize: 12; maximumLineCount: 1 }
                        }
                        AndroidGlyph { name: "arrow"; Layout.preferredWidth: 16; Layout.preferredHeight: 16; Layout.alignment: Qt.AlignVCenter; ink: app.muted }
                    }
                }
                Label {
                    id: playerTrackTitle
                    objectName: "playerTrackTitle"
                    clip: true
                    text: app.api.current?.title || ""
                    textFormat: Text.PlainText
                    color: app.ink; font.bold: true; font.pixelSize: 21
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                    // Color-emoji fallback fonts can report enormous line metrics
                    // on Android. Metadata must never displace transport controls.
                    lineHeightMode: Text.ProportionalHeight
                    lineHeight: 1
                    property FontMetrics metrics: FontMetrics { font: playerTrackTitle.font }
                    onLineLaidOut: line => {
                        const ascent = TextGeometry.lineAscent(text, font, line.width, line.number, true)
                        const step = Math.ceil(font.pixelSize * 1.35)
                        line.height = step
                        line.y = line.number * step + metrics.ascent - ascent
                    }
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Layout.minimumHeight: 0
                    Layout.preferredHeight: Math.min(implicitHeight, 58)
                    Layout.maximumHeight: 58
                }
                Caption { text: sc.displayArtist(app.api.current || {}) || ""; color: app.muted; Layout.fillWidth: true; TapHandler { enabled: !!app.api.current?.user?.urn && !sc.hasSeparateArtist(app.api.current || {}); onTapped: { player.close(); app.openProfile(app.api.current.user) } } }
                Caption {
                    Layout.fillWidth: true; visible: sc.hasSeparateArtist(app.api.current || {})
                    text: qsTr("Загрузил: ") + (app.api.current?.user?.username || "SoundCloud"); color: app.muted; font.pixelSize: 12
                    TapHandler { enabled: !!app.api.current?.user?.urn; onTapped: { player.close(); app.openProfile(app.api.current.user) } }
                }
                TrackProgress {
                    id: progress
                    Layout.fillWidth: true
                    source: player.opened ? (app.api.current.waveform_url || "") : ""
                    selectionKey: String(app.api.current.urn || "")
                    duration: sc.duration > 0 ? sc.duration : (typeof app.api.current.duration === "number" ? app.api.current.duration : 0)
                    position: sc.position || 0
                    ink: Qt.alpha(app.muted, 0.4); accent: app.accent
                    onSeekRequested: milliseconds => sc.seek(milliseconds)
                }
                RowLayout {
                    Layout.fillWidth: true
                    Caption { text: app.time(progress.value); color: app.muted; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                    Caption { text: app.time(sc.duration); color: app.muted; font.pixelSize: 12 }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 0
                    PlayerButton { text: qsTr("Перемешать"); symbol: "shuffle"; active: !!app.api.shuffle; onClicked: sc.setShuffle(!app.api.shuffle) }
                    Item { Layout.fillWidth: true }
                    PlayerButton { text: qsTr("Предыдущий трек"); symbol: "previous"; onClicked: sc.previous() }
                    Item { Layout.fillWidth: true }
                    PlayerButton { loading: !!app.api.buffering; text: app.api.playing ? qsTr("Пауза") : qsTr("Воспроизвести"); symbol: app.api.playing ? "pause" : "play"; main: true; onClicked: sc.togglePlayback() }
                    Item { Layout.fillWidth: true }
                    PlayerButton { text: qsTr("Следующий трек"); symbol: "next"; onClicked: sc.next() }
                    Item { Layout.fillWidth: true }
                    PlayerButton { ToolTip.visible: app.showTooltips && (down); ToolTip.delay: 500; ToolTip.text: Accessible.name; text: [qsTr("Повтор выкл."), qsTr("Повтор трека"), qsTr("Повтор очереди")][app.api.repeatMode || 0]; Accessible.name: [qsTr("Повтор выключен"), qsTr("Повтор трека"), qsTr("Повтор очереди")][app.api.repeatMode || 0]; objectName: "repeatModeButton"; symbol: app.api.repeatMode === 1 ? "repeatOne" : app.api.repeatMode === 2 ? "repeat" : "repeatOff"; active: app.api.repeatMode > 0; onClicked: sc.setRepeatMode(((app.api.repeatMode || 0) + 1) % 3) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    EngagementAction { host: app; mobile: true; item: app.api.current || ({}); Layout.fillWidth: true }
                    FollowButton { user: app.api.current?.user || ({}); Layout.fillWidth: true }
                    Action { text: qsTr("Очередь"); Layout.fillWidth: true; onClicked: queue.open() }
                }
            }
        }
    }
    Sheet {
        id: queue
        parent: Overlay.overlay; width: parent.width; height: parent.height * 0.8; y: parent.height - height
        modal: true; padding: 0
        background: Rectangle { color: app.surface; radius: 24 }
        contentItem: ColumnLayout {
            RowLayout {
                Layout.margins: 20; Layout.fillWidth: true
                Caption { text: qsTr("Очередь"); font.pixelSize: 19; font.bold: true; Layout.fillWidth: true }
                Action { text: qsTr("Действия"); symbol: "menu"; implicitHeight: 48; Accessible.name: qsTr("Действия с очередью"); onClicked: queueOptions.open() }
                Action { text: qsTr("Готово"); onClicked: queue.close() }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: (sc.queueTracks || []).length
                delegate: Track { required property int index; modelData: sc.queueTracks[index] || ({}); queued: true; queueIndex: index }
            }
        }
    }
    QueueActions { id: queueActionsMenu; host: app; mobile: true }
    Sheet {
        id: queueOptions
        parent: Overlay.overlay
        width: parent.width
        height: queueMenuContent.implicitHeight + topPadding + bottomPadding
        y: parent.height - height
        modal: true; padding: 16
        bottomPadding: 16 + app.safeAreaBottom(parent)
        background: Rectangle { color: app.panel; radius: 28 }
        contentItem: ColumnLayout {
            id: queueMenuContent
            spacing: 2
            MenuRow { opacity: enabled ? 1 : 0.4; glyph: "plus"; text: qsTr("Сохранить как плейлист"); enabled: sc.signedIn && (sc.queueTracks || []).length > 0 && !app.api.queuePlaylistBusy; onClicked: { queueOptions.close(); queueActionsMenu.begin() } }
            MenuRow { text: qsTr("Отменить изменение очереди"); enabled: !!app.api.queueUndoAvailable; onClicked: { sc.undoQueueEdit(); queueOptions.close() } }
            MenuRow { opacity: enabled ? 1 : 0.4; glyph: "list"; text: qsTr("Убрать повторы"); enabled: { const queue = sc.queueTracks; return sc.queueHasDuplicates() } onClicked: { sc.deduplicateQueue(); queueOptions.close() } }
            MenuRow { opacity: enabled ? 1 : 0.4; glyph: "close"; text: qsTr("Очистить после текущего"); enabled: app.api.queueIndex >= 0 && app.api.queueIndex + 1 < (sc.queueTracks || []).length; onClicked: { sc.clearQueueAfterCurrent(); queueOptions.close() } }
        }
    }
    component MenuRow: Button {
        id: menuRow
        property string glyph: "music"
        property bool multiline: false
        Layout.fillWidth: true
        implicitHeight: multiline ? Math.max(50, contentItem.implicitHeight + 20) : 50
        leftPadding: 14; rightPadding: 14
        Accessible.name: text
        background: Rectangle { radius: 14; color: menuRow.down ? app.raised : "transparent" }
        contentItem: RowLayout {
            spacing: 16
            AndroidGlyph { name: menuRow.glyph; ink: app.muted; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
            MetadataLabel { text: menuRow.text; color: app.ink; font.pixelSize: 14; Layout.fillWidth: true; wrapMode: menuRow.multiline ? Text.Wrap : Text.NoWrap; elide: menuRow.multiline ? Text.ElideNone : Text.ElideRight }
        }
    }
    Drawer {
        id: trackMenu
        objectName: "trackActionsSheet"
        edge: Qt.BottomEdge
        dragMargin: 0
        // Only the header owns dismissal; content gestures belong to Flickable.
        interactive: false
        enter: Transition { NumberAnimation { duration: app.motion; easing.type: Easing.OutCubic } }
        exit: Transition { enabled: trackMenu.position > 0; NumberAnimation { duration: app.motion; easing.type: Easing.OutCubic } }
        Overlay.modal: Rectangle { color: "#99000000" }
        onAboutToHide: menuMotion.stop()
        onAboutToShow: {
            menuMotion.stop()
            menuScroll.cancelFlick()
            menuScroll.contentY = 0
            app.lyricTrack = app.selectedTrack
            lyricsBackend.lookup(app.selectedTrack.title || "", sc.displayArtist(app.selectedTrack) || "", app.selectedTrack.duration || 0, String(app.selectedTrack.urn || app.selectedTrack.id || ""))
        }
        parent: Overlay.overlay
        width: parent.width
        height: Math.min(parent.height - Math.max(app.safeAreaTop(parent) + 24, parent.height * 0.1), menuHeader.implicitHeight + menuContent.implicitHeight + topPadding + bottomPadding)
        modal: true
        leftPadding: 16; rightPadding: 16; topPadding: 14
        bottomPadding: 12 + app.safeAreaBottom(parent)
        background: Rectangle { color: app.panel; radius: 24 }
        contentItem: Item {
            SheetMotion {
                id: menuMotion
                extent: trackMenu.height
                onValueChanged: trackMenu.position = 1 - value / Math.max(1, extent)
                onSettled: closed => { if (closed) trackMenu.close() }
            }
            ColumnLayout {
                id: menuHeader
                PointHandler {
                    onActiveChanged: {
                        if (active) {
                            menuMotion.stop()
                            menuMotion.value = (1 - trackMenu.position) * trackMenu.height
                        } else if (!menuDrag.active && !menuMotion.running && trackMenu.position < 1) {
                            menuMotion.settle(0)
                        }
                    }
                }
                DragHandler {
                    id: menuDrag
                    property real releaseSpeed: 0
                    property double lastMoveTime: 0
                    onActiveTranslationChanged: if (active) {
                        releaseSpeed = centroid.velocity.y
                        lastMoveTime = Date.now()
                    }
                    target: null
                    enabled: !menuScroll.dragging
                    xAxis.enabled: false
                    property real pressY: 0
                    property real startOffset: 0
                    onActiveChanged: {
                        if (active) {
                            menuMotion.stop()
                            menuScroll.cancelFlick()
                            startOffset = (1 - trackMenu.position) * trackMenu.height
                            pressY = centroid.scenePosition.y
                        } else menuMotion.settle(Date.now() - lastMoveTime < 100 ? releaseSpeed : 0)
                    }
                    onCentroidChanged: if (active) menuMotion.value = Math.max(0,
                        Math.min(trackMenu.height, startOffset + centroid.scenePosition.y - pressY))
                }
                objectName: "trackActionsHeader"
                width: parent.width
                spacing: 0
                Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 18; implicitWidth: 32; implicitHeight: 4; radius: 2; color: Qt.alpha(app.ink, 0.25) }
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 6; Layout.rightMargin: 2; Layout.bottomMargin: 18
                    spacing: 12
                    RoundedImage { objectName: "trackActionsCover"; source: app.cover(app.selectedTrack); radius: 10; Layout.preferredWidth: 48; Layout.preferredHeight: 48; placeholderInk: app.muted }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 5
                        MetadataLabel { objectName: "trackActionsTitle"; text: app.selectedTrack.title || qsTr("Без названия"); textFormat: Text.PlainText; color: app.ink; font.pixelSize: 16; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                        MetadataLabel { objectName: "trackActionsArtist"; text: sc.displayArtist(app.selectedTrack) || "SoundCloud"; textFormat: Text.PlainText; color: app.muted; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    }
                    PlayerButton { symbol: "close"; text: qsTr("Закрыть меню"); Layout.preferredWidth: 40; Layout.preferredHeight: 44; onClicked: trackMenu.close() }
                }
                Rectangle { Layout.fillWidth: true; Layout.bottomMargin: 8; implicitHeight: 1; color: Qt.alpha(app.ink, 0.08) }
            }
            Flickable {
                id: menuScroll
                objectName: "trackActionsScroll"
                anchors.top: menuHeader.bottom; anchors.bottom: parent.bottom
                width: parent.width
                clip: true; contentWidth: width; contentHeight: menuContent.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                ScrollIndicator.vertical: ScrollIndicator { }
                ColumnLayout {
                    id: menuContent
                    width: parent.width; spacing: 0
                    MenuRow { glyph: "info"; text: qsTr("Информация о треке"); onClicked: { trackInfoSheet.track = app.selectedTrack; trackMenu.close(); trackInfoSheet.open() } }
                    MenuRow { glyph: "comment"; text: qsTr("Комментарии"); onClicked: { commentsSheet.selectedTrack = app.selectedTrack; commentsSheet.followCurrent = app.selectedTrack.urn === app.api.current.urn; trackMenu.close(); commentsSheet.open() } }
                    MenuRow { glyph: "list"; text: qsTr("Текст песни"); visible: (app.lyricState.lines || []).length > 0; onClicked: { trackMenu.close(); lyricsSheet.open() } }
                    EngagementAction { host: app; mobile: true; menuPresentation: true; Layout.fillWidth: true; item: app.selectedTrack; repost: true }
                    EngagementAction { host: app; mobile: true; menuPresentation: true; item: app.selectedTrack; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; Layout.bottomMargin: 8; implicitHeight: 1; color: Qt.alpha(app.ink, 0.08) }
                    MenuRow { glyph: "close"; text: qsTr("Не предлагать этот трек"); enabled: !!app.selectedTrack.urn; onClicked: { sc.excludeRecommendation(app.selectedTrack, false); trackMenu.close() } }
                    MenuRow { multiline: true; glyph: "close"; text: qsTr("Не предлагать треки этого аккаунта: ") + (app.selectedTrack.user?.username || app.selectedTrack.user?.urn || "SoundCloud"); enabled: !!app.selectedTrack.user?.urn; onClicked: { sc.excludeRecommendation(app.selectedTrack, true); trackMenu.close() } }
                    MenuRow { glyph: "next"; visible: app.selectedQueue >= 0; text: qsTr("Играть следующим"); Layout.fillWidth: true; onClicked: { sc.queueNext(app.selectedQueue); trackMenu.close() } }
                    MenuRow { glyph: "close"; visible: app.selectedQueue >= 0; text: qsTr("Удалить из очереди"); Layout.fillWidth: true; onClicked: { sc.removeQueue(app.selectedQueue); trackMenu.close() } }
                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; Layout.bottomMargin: 8; implicitHeight: 1; color: Qt.alpha(app.ink, 0.08) }
                    MenuRow { glyph: "plus"; text: qsTr("Добавить в очередь"); Layout.fillWidth: true; onClicked: { sc.enqueue(app.selectedTrack); trackMenu.close() } }
                    MenuRow { glyph: "music"; text: qsTr("Сохранить в плейлист"); Layout.fillWidth: true; onClicked: { sc.listDestinations(); destinations.open(); trackMenu.close() } }
                    MenuRow { glyph: "profile"; text: sc.hasSeparateArtist(app.selectedTrack) ? qsTr("Загрузил: ") + (app.selectedTrack.user?.username || "SoundCloud") : qsTr("Профиль исполнителя"); Layout.fillWidth: true; onClicked: { app.openProfile(app.selectedTrack.user); trackMenu.close(); playlistView.close(); player.close() } }
                    MenuRow { glyph: "play"; text: app.selectedTrack.station_urn ? qsTr("Станция по треку") : qsTr("Похожие треки"); Layout.fillWidth: true; onClicked: { trackMenu.close(); playlistView.beginStation(app.selectedTrack, false) } }
                    MenuRow { glyph: "cloud"; text: qsTr("Открыть в SoundCloud"); Layout.fillWidth: true; onClicked: { sc.openExternal(app.selectedTrack.permalink_url); trackMenu.close() } }
                    MenuRow { glyph: "copy"; text: qsTr("Копировать название и исполнителя"); Layout.fillWidth: true; onClicked: { sc.copyText((app.selectedTrack.title || qsTr("Без названия")) + " — " + (sc.displayArtist(app.selectedTrack) || "SoundCloud")); trackMenu.close() } }
                    MenuRow { glyph: "link"; text: qsTr("Копировать ссылку"); enabled: /^https:\/\/([a-z0-9-]+\.)*soundcloud\.com\//i.test(app.selectedTrack.permalink_url || ""); Layout.fillWidth: true; onClicked: { sc.copyText(app.selectedTrack.permalink_url); trackMenu.close() } }
                }
            }
        }
    }
    Sheet {
        id: trackInfoSheet
        objectName: "trackInfoSheet"
        property var track: ({})
        parent: Overlay.overlay
        width: parent.width
        height: Math.min(parent.height - app.safeAreaTop(parent) - 12, 720)
        y: parent.height - height
        modal: true; focus: true; padding: 20
        bottomPadding: 16 + app.safeAreaBottom(parent)
        background: Rectangle { color: app.panel; radius: 24 }
        onOpened: infoScroll.contentY = 0
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                MetadataLabel { text: qsTr("Информация о треке"); color: app.ink; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                PlayerButton { symbol: "close"; text: qsTr("Закрыть сведения"); onClicked: trackInfoSheet.close() }
            }
            Flickable {
                id: infoScroll
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
                clip: true; contentWidth: width; contentHeight: infoBody.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                ColumnLayout {
                    id: infoBody; width: parent.width; spacing: 20
                    MetadataLabel { text: trackInfoSheet.track.title || ""; color: app.ink; font.pixelSize: 19; font.bold: true; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    TrackInformation {
                        Layout.fillWidth: true; track: trackInfoSheet.track; host: app
                        onProfileRequested: user => { trackInfoSheet.close(); player.close(); app.openProfile(user) }
                    }
                }
            }
        }
    }
    Sheet {
        id: commentsSheet
        objectName: "commentsSheet"
        parent: Overlay.overlay
        width: parent.width; height: parent.height
        modal: true; focus: true
        padding: 16
        topPadding: 12 + app.safeAreaTop(parent)
        bottomPadding: 12 + app.safeAreaBottom(parent)
        property bool followCurrent: true
        property var selectedTrack: ({})
        readonly property var track: followCurrent ? app.api.current : selectedTrack
        background: Rectangle { color: app.surface }
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                PlayerButton { symbol: "back"; text: qsTr("Закрыть комментарии"); onClicked: commentsSheet.close() }
                Caption { Layout.fillWidth: true; text: commentsSheet.track.title || ""; font.pixelSize: 13; color: app.muted; textFormat: Text.PlainText }
            }
            CommentsPane {
                id: mobileComments
                objectName: "mobileCommentsPane"
                host: app; preferences: prefs; mobile: true
                track: commentsSheet.track
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
                onProfileRequested: function(user) { commentsSheet.close(); player.close(); app.openProfile(user) }
            }
        }
    }
    Sheet {
        id: lyricsSheet
        onOpened: { lyricScroll.cancelFlick(); lyricScroll.contentY = 0 }
        parent: Overlay.overlay
        width: parent.width; height: parent.height
        modal: true; focus: true
        padding: 20
        topPadding: 12 + app.safeAreaTop(parent)
        bottomPadding: 16 + app.safeAreaBottom(parent)
        background: Rectangle { color: app.surface }
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    Caption { text: qsTr("Текст песни"); font.pixelSize: 24; font.bold: true; Layout.fillWidth: true }
                    Caption { text: (app.lyricTrack.title || "") + " · " + (sc.displayArtist(app.lyricTrack) || ""); color: app.muted; font.pixelSize: 12; Layout.fillWidth: true }
                }
                PlayerButton { symbol: "close"; text: qsTr("Закрыть текст песни"); onClicked: lyricsSheet.close() }
            }
            Flickable {
                id: lyricScroll
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumHeight: 0; Layout.preferredHeight: 0
                clip: true
                contentWidth: width
                contentHeight: lyricLines.implicitHeight + 48
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.DragOverBounds
                boundsMovement: Flickable.FollowBoundsBehavior
                pressDelay: 0
                pixelAligned: false
                Column {
                    id: lyricLines
                    x: 4; y: 16; width: Math.max(0, lyricScroll.width - 20)
                    spacing: 22
                    Repeater {
                        model: app.lyricState.lines || []
                        delegate: Text {
                            required property var modelData
                            width: lyricLines.width
                            text: modelData.text || ""
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: app.ink; font.pixelSize: 22; font.weight: Font.Medium
                            lineHeight: 1.25
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {
                    width: 3
                    policy: ScrollBar.AsNeeded
                    contentItem: Rectangle { implicitWidth: 3; radius: 1.5; color: app.muted; opacity: lyricScroll.moving ? 0.65 : 0.25; Behavior on opacity { NumberAnimation { duration: 180 } } }
                }
            }
            Caption { text: app.lyricState.custom ? qsTr("Сохранённый текст") : qsTr("Источник: ") + (app.lyricState.source || ""); color: app.muted; font.pixelSize: 11; Layout.fillWidth: true }
        }
    }
    Sheet {
        id: createPlaylist
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Math.max(16 + app.safeAreaTop(parent), (parent.height - Qt.inputMethod.keyboardRectangle.height - height) / 2)
        width: Math.min(parent.width - 32, 380)
        modal: true; padding: 20
        background: Rectangle { radius: 24; color: app.surface }
        onOpened: { playlistTitle.clear(); playlistTitle.forceActiveFocus() }
        onClosed: Qt.inputMethod.hide()
        function submit() {
            if (!playlistTitle.text.trim()) return
            sc.createPlaylist(playlistTitle.text.trim(), false)
            close()
        }
        contentItem: ColumnLayout {
            spacing: 16
            Caption { text: qsTr("Новый плейлист"); font.pixelSize: 20; font.bold: true; Layout.fillWidth: true }
            TextField {
                id: playlistTitle
                Layout.fillWidth: true; implicitHeight: 48
                placeholderText: qsTr("Название плейлиста"); maximumLength: 200
                font.pixelSize: 14; color: app.ink; placeholderTextColor: (app.dark ? "#aaaaaa" : "#77717a")
                selectionColor: app.accent; selectedTextColor: app.foreground(app.accent); leftPadding: 14
                background: Rectangle { radius: 16; color: (app.dark ? "#242424" : "#eeeeee"); border.width: playlistTitle.activeFocus ? 1 : 0; border.color: Qt.alpha(app.accent, 0.5) }
                onAccepted: createPlaylist.submit()
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Action { text: qsTr("Отмена"); Layout.fillWidth: true; onClicked: createPlaylist.close() }
                Action { text: qsTr("Создать"); primary: true; Layout.fillWidth: true; enabled: !!playlistTitle.text.trim(); onClicked: createPlaylist.submit() }
            }
        }
    }
    Sheet {
        id: destinations
        parent: Overlay.overlay; width: parent.width; height: parent.height * 0.7; y: parent.height - height
        modal: true; padding: 20
        background: Rectangle { radius: 24; color: app.panel }
        contentItem: ColumnLayout {
            Caption { text: qsTr("Сохранить в плейлист"); font.bold: true; font.pixelSize: 22 }
            MetadataLabel { visible: !!app.api.loadingDestinations; text: qsTr("Загрузка…"); color: app.muted }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: app.api.destinations || []
                delegate: ItemDelegate {
                    
                    required property var modelData
                    width: ListView.view.width; height: 56; text: modelData.title
                    onClicked: { sc.saveToPlaylist(app.selectedTrack.urn, modelData.urn, ""); destinations.close() }
                }
                footer: Action { visible: !!app.api.destinationNext; text: qsTr("Ещё"); onClicked: sc.listDestinations(app.api.destinationNext) }
            }
            Action { text: qsTr("Закрыть"); Layout.fillWidth: true; onClicked: destinations.close() }
        }
    }
    Sheet {
        id: account
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(parent.width - 40, 380); modal: true; padding: 24
        background: Rectangle { color: app.panel; radius: 24 }
        contentItem: ColumnLayout {
            Caption { text: app.api.profile?.username || "Sange"; font.pixelSize: 19; font.bold: true; Layout.fillWidth: true }
            MetadataLabel { text: qsTr("SoundCloud для твоего ритма"); color: app.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Action { text: sc.signedIn ? qsTr("Выйти из аккаунта") : qsTr("Войти в SoundCloud"); Layout.fillWidth: true; onClicked: { sc.signedIn ? sc.logout() : sc.login(); account.close() } }
            Action { text: qsTr("Закрыть"); Layout.fillWidth: true; onClicked: account.close() }
        }
    }
    PluginToolsPopup { dark: app.dark; ink: app.ink; muted: app.muted; id: pluginTools; manager: pluginManager }
    Connections {
        target: pluginManager
        function onNavigationRequested(destination) {
            switch (destination) {
            case "profile": app.openProfile(null); break
            case "search": app.page = 2; search.forceActiveFocus(); break
            case "feed": app.page = 1; break
            case "likes": case "history": case "playlists": app.library = destination; app.page = 3; app.refresh(); break
            }
            pluginTools.close()
        }
    }
    Connections {
        target: sc
        function onSignedInChanged() { app.page = 0; if (sc.signedIn) sc.loadHome(); else { app.scrollPositions = ({}); player.close(); queue.close() } }
    }
    Sheet {
        id: startup
        swipeDismissEnabled: false
        parent: Overlay.overlay
        width: parent.width; height: parent.height
        modal: true; closePolicy: Popup.NoAutoClose; padding: 0
        background: Rectangle { color: app.surface }
        contentItem: Item {
            Column {
                anchors.centerIn: parent; spacing: 14
                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: "qrc:/qt/qml/SCrip/assets/blob.png"
                    width: 88; height: 88; fillMode: Image.PreserveAspectFit
                    Rectangle { anchors.fill: parent; anchors.margins: -16; radius: 32; color: app.accent; z: -1 }

                }
                Caption { anchors.horizontalCenter: parent.horizontalCenter; text: "Sange"; font.pixelSize: 25; font.bold: true }
                Caption { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Твоя музыка рядом"); color: app.muted; font.pixelSize: 13 }
            }
        }
        Timer { interval: 900; running: startup.opened; onTriggered: startup.close() }
    }
    Component.onCompleted: { if (androidMedia) androidMedia.setDarkTheme(dark); startup.open(); if (sc.signedIn) sc.loadHome() }
    RecommendationPreferences {
        id: recommendationSettings
        backend: sc
        ink: app.ink; surface: app.raised; accent: app.accent; selectedInk: app.foreground(app.accent)
    }
    RecommendationNotice { backend: sc; ink: app.ink; surface: app.raised }

}
