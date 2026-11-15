import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Controls.impl as ControlsImpl
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Dialogs as Dialogs
import QtCore

Window {
    id: app
    width: 1320; height: 820
    minimumWidth: 760; minimumHeight: 560
    visible: true
    title: "SCrip"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    palette.window: app.panel
    palette.windowText: app.ink
    palette.text: app.ink
    palette.base: app.surface
    palette.button: app.raised
    palette.buttonText: app.ink
    palette.highlight: app.accent
    palette.highlightedText: app.foreground(app.accent)
    readonly property var api: sc.catalogState
    readonly property var extensions: typeof pluginManager === "undefined" ? null : pluginManager
    readonly property var pluginTheme: extensions ? extensions.theme : ({})
    readonly property var themes: JSON.parse(prefs.savedThemes)
    readonly property var chosenTheme: themes.find(t => t.name === prefs.themeName)
    readonly property color baseAccent: chosenTheme?.accent ?? pluginTheme.accent ?? "#f43756"
    property color accent: (dark ? Qt.tint(baseAccent, Qt.rgba(1, 1, 1, 0.28)) : baseAccent)
    readonly property bool dark: prefs.darkMode
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
            }
        }
    }

    readonly property bool themed: !!chosenTheme || !!pluginTheme.accent
    property color surface: (dark ? Qt.tint("#111111", Qt.alpha(baseAccent, chosenTheme ? Math.min(0.06, chosenTheme.tint / 300) : 0)) : themed ? Qt.tint("white", Qt.alpha(baseAccent, chosenTheme ? chosenTheme.tint / 100 : 0.05)) : "white")
    property color panel: dark ? "#1a1a1a" : themed ? Qt.tint(surface, Qt.alpha(accent, 0.05)) : "#f7f7f8"
    property color raised: (dark ? "#282828" : "#f0edef")
    readonly property color line: (dark ? "#383838" : "#e6e2e5")
    function foreground(c) {
        function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b) > 0.179 ? "#111113" : "#ffffff"
    }
    readonly property int artworkRadius: prefs.coverRadiusOverride ? prefs.coverRadius : pluginTheme.coverRadius ?? prefs.coverRadius
    property string pluginMessage: ""

    property color ink: (dark ? "#f1f1f1" : "#242426")
    Behavior on surface { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    Behavior on panel { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    Behavior on raised { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    Behavior on ink { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    Behavior on accent { ColorAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
    readonly property color muted: (dark ? "#aaaaaa" : "#858589")
    property int section: 5
    property int category: 0
    property int settingGroup: 0
    property string query: ""
    property string collectionQuery: ""
    readonly property var filteredCollection: {
        const needle = collectionQuery.trim().toLocaleLowerCase()
        const tracks = api.tracks || []
        return needle ? tracks.filter(track => [track.title, track.user?.username, track.username]
            .some(value => (value || "").toLocaleLowerCase().includes(needle))) : tracks
    }
    readonly property var playableCollection: filteredCollection.filter(track => (track.urn || "").startsWith("soundcloud:tracks:"))
    property string libraryPage: "likes"
    property int profileTab: 0
    property var guest: null
    readonly property var displayedProfile: guest ? (api.visitedProfile?.urn === guest.urn ? api.visitedProfile : guest) : api.profile
    function openTrack(track, followCurrent = false) {
        if (!track || !(track.urn || track.id)) return
        if (track.kind === "playlist" || (track.urn || "").startsWith("soundcloud:playlists:")) { playlistView.begin(track); return }
        trackPage.changingSelection = true
        trackPage.followCurrent = followCurrent
        trackPage.selectedTrack = track
        trackPage.changingSelection = false
        if (trackPage.opened) trackPage.loadLyrics()
        trackPage.open()
    }
    function openTrackActions(track) { trackActions.track = track; trackActions.open() }
    function openProfile(user) {
        guest = user && user.urn !== api.profile.urn ? user : null
        const reload = section === 3 && profileTab === 0
        profileTab = 0
        section = 3
        if (reload) refreshPage()
    }
    readonly property string scrollKey: section + ":" + (section === 1 ? libraryPage : section === 0 ? category + ":" + query : section === 3 ? profileTab + ":" + (guest?.urn || "me") : "")
    property var scrollPositions: ({})
    property string activeScrollKey: scrollKey
    property bool restoringScroll: false
    function restorePageScroll() {
        contentScroll.wheelTarget = 0
        const saved = scrollPositions[activeScrollKey] || { outer: 0, inner: 0 }
        Qt.callLater(function() {
            contentScroll.contentY = saved.outer
            if (collectionLoader.item && collectionLoader.item.contentY !== undefined)
                collectionLoader.item.contentY = saved.inner
            app.restoringScroll = false
        })
    }
    onScrollKeyChanged: {
        collectionQuery = ""
        restoringScroll = true
        activeScrollKey = scrollKey
        restorePageScroll()
    }
    property bool inspectorOpen: true
    readonly property bool dockedInspector: width >= 1100 && inspectorOpen
    readonly property int inspectorWidth: dockedInspector ? (width >= 1300 ? 280 : 256) : 0
    readonly property int gutter: 24
    readonly property int motion: prefs.motion ? prefs.animationMs : 0
    readonly property string pageTitle: section === 4 && settingGroup === 1 ? "Плагины" : section === 1 ? (libraryPage === "likes" ? "Любимые треки" : libraryPage === "history" ? "Недавно слушали" : "Лента") : pages[section]
    property bool lastSignedIn: false
    readonly property var pages: ["Поиск", "Любимые треки", "Плейлисты", "Профиль", "Настройки", "Главная"]
    readonly property bool compact: width < 1000

    PlaylistPopup {
        id: playlistView
        parent: Overlay.overlay
        host: app
    }
    Settings { id: prefs; objectName: "uiPreferences"; category: "appleInterface"; property bool gridView: true; property real volume: 0.7; property bool motion: true
        property string hiddenMixTracks: "{}"
        property int spectrumStyle: 0
        property bool darkMode: true
        property string savedThemes: "[]"
        property string themeName: ""
        property int animationMs: 240
        property bool themePreferencesMigrated: false
        Component.onCompleted: {
            if (!themePreferencesMigrated) { coverRadiusOverride = coverRadius !== 8; themePreferencesMigrated = true }
        }
        property bool coverRadiusOverride: false
        property int coverRadius: 8
        property int avatarRoundness: 50
        property int panelRadius: 12
        property bool autoLoad: true

    }
    function time(ms) { const s = Math.max(0, Math.floor((ms || 0) / 1000)); return Math.floor(s / 60) + ":" + (s % 60).toString().padStart(2, "0") }
    function refreshPage(force = false) {
        if (!sc.signedIn) return
        if (section === 0) sc.browse(["tracks", "sets", "users"][category], query, force)
        else if (section === 1) sc.browse(libraryPage, "", force)
        else if (section === 2) sc.browse("playlists", "", force)
        else if (section === 3) sc.browse(guest ? ["userTracks", "userSets"][profileTab] : ["mine", "playlists", "likes", "reposts"][profileTab], guest?.urn || "", force)
        else if (section === 5) sc.loadHome(force)
    }
    function searchNow() { query = searchField.text; if (section !== 0) section = 0; else refreshPage() }
    function selectLibrary(page) { libraryPage = page; if (section !== 1) section = 1; else refreshPage() }
    function imageUrl(url) {
        if (!url) return ""
        return url.replace("-large.", "-t300x300.")
    }
    function cover(item) { if (pluginTheme.covers === "artist" && item.user?.avatar_url) return imageUrl(item.user.avatar_url); return imageUrl(item.artwork_url || item.tracks?.[0]?.artwork_url || item.user?.avatar_url || "") }
    function maximize() { if (visibility === Window.Maximized) showNormal(); else showMaximized() }
    onSectionChanged: { trackPage.close(); refreshPage(); animatePage() }
    function animatePage() { if (prefs.motion && !pageTransition.running) pageTransition.start() }
    onSettingGroupChanged: animatePage()
    onProfileTabChanged: { if (section === 3) { refreshPage(); animatePage() } }
    onDockedInspectorChanged: { if (dockedInspector) queuePopup.close() }
    function toggleInspector() {
        if (width >= 1100) inspectorOpen = !inspectorOpen
        else if (queuePopup.opened) queuePopup.close(); else queuePopup.open()
    }
    onCategoryChanged: { if (section === 0) { refreshPage(); animatePage() } }
    Connections {
        target: sc
        function onSignedInChanged() {
            const newlySignedIn = sc.signedIn && !app.lastSignedIn
            app.lastSignedIn = sc.signedIn
            if (!sc.signedIn) app.scrollPositions = ({})
            if (newlySignedIn) { app.section = 5; Qt.callLater(app.refreshPage); app.requestActivate() }
        }
    }
    Component.onCompleted: { lastSignedIn = sc.signedIn; if (lastSignedIn) refreshPage() }
    Shortcut { sequences: [StandardKey.Find]; onActivated: { searchField.forceActiveFocus(); searchField.selectAll() } }
    Shortcut { sequence: "F11"; onActivated: app.visibility === Window.FullScreen ? app.showNormal() : app.showFullScreen() }

    component Txt: Text {
        color: app.ink; font.pixelSize: 13; textFormat: Text.PlainText
        elide: Text.ElideRight; renderType: Text.QtRendering
    }
    component Glyph: Canvas {
        property string name: "music"
        property color ink: app.ink
        width: 20; height: 20
        onNameChanged: requestPaint()
        onInkChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset(); c.scale(width / 24, height / 24)
            c.strokeStyle = ink; c.fillStyle = ink; c.lineWidth = 1.65; c.lineCap = "round"; c.lineJoin = "round"; c.beginPath()
            switch (name) {
            case "search": c.arc(10,10,6,0,Math.PI*2); c.moveTo(15,15); c.lineTo(21,21); break
            case "home": c.moveTo(3,11); c.lineTo(12,3); c.lineTo(21,11); c.moveTo(5,10); c.lineTo(5,21); c.lineTo(10,21); c.lineTo(10,14); c.lineTo(14,14); c.lineTo(14,21); c.lineTo(19,21); c.lineTo(19,10); break
            case "liked": case "heart": c.moveTo(12,21); c.bezierCurveTo(-5,10,5,-2,12,7); c.bezierCurveTo(19,-2,29,10,12,21); if (name === "liked") c.fill(); break
            case "music": c.moveTo(9,17); c.lineTo(9,5); c.lineTo(20,3); c.lineTo(20,15); c.moveTo(9,8); c.lineTo(20,6); c.moveTo(9,17); c.ellipse(6,18,3,2,0,0,Math.PI*2); c.moveTo(20,15); c.ellipse(17,16,3,2,0,0,Math.PI*2); break
            case "grid": for(let y=4;y<20;y+=9) for(let x=4;x<20;x+=9) c.rect(x,y,6,6); break
            case "list": for(let y=6;y<21;y+=6) { c.moveTo(8,y); c.lineTo(21,y); c.moveTo(3,y); c.lineTo(3.2,y) } break
            case "clock": c.arc(12,12,9,0,Math.PI*2); c.moveTo(12,6); c.lineTo(12,12); c.lineTo(16,14); break
            case "profile": c.arc(12,8,3.5,0,Math.PI*2); c.moveTo(4,21); c.bezierCurveTo(4,12,20,12,20,21); break
            case "settings": for(let y=5;y<22;y+=7) { const x=y===12?15:8; c.moveTo(3,y); c.lineTo(x-2,y); c.moveTo(x+2,y); c.lineTo(21,y); c.moveTo(x+2,y); c.arc(x,y,2,0,Math.PI*2) } break
            case "play": c.moveTo(8,4); c.lineTo(20,12); c.lineTo(8,20); c.closePath(); c.fill(); return
            case "pause": c.rect(6,4,4,16); c.rect(14,4,4,16); c.fill(); return
            case "next": c.moveTo(4,5); c.lineTo(14,12); c.lineTo(4,19); c.closePath(); c.fill(); c.moveTo(16,5); c.lineTo(16,19); break
            case "previous": c.moveTo(20,5); c.lineTo(10,12); c.lineTo(20,19); c.closePath(); c.fill(); c.moveTo(8,5); c.lineTo(8,19); break
            case "menu": for (let y = 6; y <= 18; y += 6) { c.moveTo(4,y); c.lineTo(20,y) } break
            case "plus": c.moveTo(12,5); c.lineTo(12,19); c.moveTo(5,12); c.lineTo(19,12); break
            case "close": c.moveTo(6,6); c.lineTo(18,18); c.moveTo(6,18); c.lineTo(18,6); break
            case "volume": c.moveTo(4,9); c.lineTo(8,9); c.lineTo(13,5); c.lineTo(13,19); c.lineTo(8,15); c.lineTo(4,15); c.closePath(); c.moveTo(17,8); c.quadraticCurveTo(22,12,17,16); break
            case "repeat": c.moveTo(5,9); c.lineTo(5,6); c.lineTo(19,6); c.lineTo(19,10); c.moveTo(16,7); c.lineTo(19,10); c.lineTo(22,7); c.moveTo(19,15); c.lineTo(19,18); c.lineTo(5,18); c.lineTo(5,14); c.moveTo(2,17); c.lineTo(5,14); c.lineTo(8,17); break
            case "shuffle": c.moveTo(3,6); c.bezierCurveTo(12,6,12,18,21,18); c.moveTo(18,15); c.lineTo(21,18); c.lineTo(18,21); c.moveTo(3,18); c.bezierCurveTo(12,18,12,6,21,6); c.moveTo(18,3); c.lineTo(21,6); c.lineTo(18,9); break
            case "cloud": c.moveTo(6,18); c.bezierCurveTo(-1,18,0,10,6,10); c.bezierCurveTo(6,1,19,1,19,10); c.bezierCurveTo(25,10,25,18,19,18); c.closePath(); break
            case "back": c.translate(24,0); c.scale(-1,1);
            case "arrow": c.moveTo(5,12); c.lineTo(19,12); c.moveTo(14,7); c.lineTo(19,12); c.lineTo(14,17); break
            }
            c.stroke()
        }
    }
    component Tool: AbstractButton {
        InteractionFeedback {}
        id: tool
        property string symbol: ""
        property bool active: false
        property bool primary: false
        property bool round: false
        implicitWidth: symbol ? 32 : Math.max(76, toolLabel.implicitWidth + 24)
        implicitHeight: 30
        hoverEnabled: true
        Accessible.name: text
        opacity: enabled ? 1 : 0.4
        Behavior on opacity { NumberAnimation { duration: app.motion * 0.6; easing.type: Easing.OutCubic } }
        scale: down && app.motion > 0 ? 0.965 : 1
        Behavior on scale { NumberAnimation { duration: app.motion / 2; easing.type: Easing.OutCubic } }
        background: Rectangle {
            radius: tool.round ? height / 2 : 7
            color: tool.primary ? (tool.down ? Qt.darker(app.accent, 1.12) : app.accent)
                 : tool.down || tool.active ? app.raised : tool.hovered ? app.panel : "transparent"
            border.color: tool.primary || tool.symbol ? "transparent" : (app.dark ? "#404040" : "#dedbde")

            Behavior on color { ColorAnimation { duration: app.motion / 2 } }
        }
        contentItem: Item {
            Glyph { visible: !!tool.symbol; anchors.centerIn: parent; width: 18; height: 18; name: tool.symbol; ink: tool.primary ? app.foreground(app.accent) : tool.active ? app.accent : app.ink }
            Txt { id: toolLabel; visible: !tool.symbol; anchors.centerIn: parent; width: Math.min(implicitWidth, tool.width - 16); text: tool.text; color: tool.primary ? app.foreground(app.accent) : app.ink; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
        }
    }
    component LikeButton: Tool {
        required property var track
        active: !!track.user_favorite
        symbol: active ? "liked" : "heart"
        text: active ? "Лайк поставлен · Убрать" : "Поставить лайк"
        Accessible.checkable: true
        Accessible.checked: active
        onClicked: sc.like(track)
    }
    component Field: TextField {
        height: 34; color: app.ink; font.pixelSize: 13; selectByMouse: true
        leftPadding: 10; rightPadding: 10; placeholderTextColor: (app.dark ? "#aaaaaa" : "#9a969b")
        background: Rectangle { radius: 12; color: app.surface; border.color: parent.activeFocus ? app.accent : (app.dark ? "#404040" : "#dddadd"); Behavior on border.color { ColorAnimation { duration: app.motion * 0.6 } } }
    }
    component LineSlider: Slider {
        id: lineSlider
        implicitHeight: 22; padding: 0
        background: Rectangle {
            x: lineSlider.leftPadding; y: (lineSlider.height-height)/2
            width: lineSlider.availableWidth; height: 3; radius: 2; color: (app.dark ? "#454545" : "#d7d4d6")
            Rectangle { width: lineSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: (app.dark ? "#aaaaaa" : "#777278") }
        }
        handle: Rectangle {
            x: lineSlider.visualPosition * (lineSlider.availableWidth - width); y: (lineSlider.height-height)/2
            width: 8; height: 8; radius: 4; color: (app.dark ? "#aaaaaa" : "#777278"); opacity: lineSlider.hovered || lineSlider.pressed || lineSlider.visualFocus ? 1 : 0
            scale: lineSlider.pressed && prefs.motion ? 1.4 : 1
            Behavior on opacity { NumberAnimation { duration: app.motion / 2 } }
            Behavior on scale { NumberAnimation { duration: app.motion / 2; easing.type: Easing.OutCubic } }
        }
    }
    component VolumeSlider: LineSlider {
        readonly property real level: app.api.volume || 0
        value: level <= 0.3 ? level / 0.6 : 0.5 + (level - 0.3) / 1.4
        live: true
        snapMode: Slider.NoSnap
        onMoved: sc.volume(value <= 0.5 ? value * 0.6 : 0.3 + (value - 0.5) * 1.4)
        Accessible.name: "Громкость"
        Accessible.description: Math.round(level * 100) + "%"
    }
    component NavItem: AbstractButton {
        id: navItem
        property string symbol: "music"
        property bool selected: false
        width: parent.width; height: 36; hoverEnabled: true
        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: 12
                color: app.ink
                opacity: !navItem.enabled ? 0 : navItem.down ? 0.12
                       : navItem.selected ? 0.075 : navItem.hovered ? 0.04 : 0
                Behavior on opacity {
                    NumberAnimation { duration: app.motion * 0.75; easing.type: Easing.OutCubic }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: 12
                color: "transparent"
                border.color: app.accent
                opacity: navItem.visualFocus ? 0.8 : 0
                Behavior on opacity { NumberAnimation { duration: app.motion * 0.5 } }
            }
        }
        contentItem: Row {
            leftPadding: 10; spacing: 9
            Glyph { anchors.verticalCenter: parent.verticalCenter; width: 17; height: 17; name: navItem.symbol; ink: navItem.selected ? app.accent : (app.dark ? "#c2c2c2" : "#454247") }
            Txt { width: navItem.width - 44; anchors.verticalCenter: parent.verticalCenter; text: navItem.text; color: navItem.selected ? app.accent : app.ink; font.pixelSize: 12 }
        }
    }

    component CapsuleSection: Pane {
        id: capsule
        default property alias contents: sectionBody.data
        padding: 18
        implicitWidth: 320
        background: Rectangle {
            radius: 22
            color: app.panel

        }
        contentItem: Column {
            id: sectionBody
            spacing: 12
        }
    }

    Rectangle {
        id: shell
        objectName: "applicationBackground"
        anchors.fill: parent; radius: app.visibility === Window.Maximized || app.visibility === Window.FullScreen ? 0 : 14
        color: app.surface; border.color: (app.dark ? "#3b3b3b" : "#d2cfd3"); clip: true

        Rectangle {
            id: sidebar
            objectName: "navigationColumn"
            x: 12; y: 12; width: 196; height: parent.height - 24
            visible: sc.signedIn; color: app.panel; radius: 28
            border.color: (app.dark ? "#252525" : "#eeecef")

            Column {
                x: 16; y: 64; width: parent.width - 32; spacing: 4
                NavItem { text: "Главная"; symbol: "home"; selected: app.section === 5; onClicked: app.section = 5 }
                NavItem { text: "Поиск"; symbol: "search"; selected: app.section === 0; onClicked: { app.section = 0; searchField.forceActiveFocus() } }
                NavItem { text: "Лента"; symbol: "list"; selected: app.section === 1 && app.libraryPage === "feed"; onClicked: app.selectLibrary("feed") }
                Item { width: 1; height: 20 }
                Txt { text: "Медиатека"; color: app.muted; font.pixelSize: 11; leftPadding: 10; height: 23 }
                NavItem { text: "Любимые треки"; symbol: "heart"; selected: app.section === 1 && app.libraryPage === "likes"; onClicked: app.selectLibrary("likes") }
                NavItem { text: "Недавние"; symbol: "clock"; selected: app.section === 1 && app.libraryPage === "history"; onClicked: app.selectLibrary("history") }
                NavItem { text: "Плейлисты"; symbol: "grid"; selected: app.section === 2; onClicked: app.section = 2 }
                NavItem { text: "Мой профиль"; symbol: "profile"; selected: app.section === 3 && !app.guest; onClicked: app.openProfile(null) }
                Item { width: 1; height: 20 }
                Txt { text: "Приложение"; color: app.muted; font.pixelSize: 11; leftPadding: 10; height: 23 }
                NavItem { text: "Настройки"; symbol: "settings"; selected: app.section === 4; onClicked: { app.settingGroup = 0; app.section = 4 } }
            }
            AbstractButton {
                InteractionFeedback {}
                x: 14; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
                width: parent.width - 28; height: 38; onClicked: app.openProfile(null)
                contentItem: Row {
                    spacing: 8
                    Avatar { width: 28; height: 28; anchors.verticalCenter: parent.verticalCenter; source: app.imageUrl(app.api.profile.avatar_url || "") }
                    Txt { width: parent.width - 38; anchors.verticalCenter: parent.verticalCenter; text: app.api.profile.username || "Аккаунт"; font.pixelSize: 12 }
                }
            }
        }
        Item {
            id: titlebar
            x: 0; y: 0; width: parent.width; height: 56
            MouseArea { anchors.fill: parent; onPressed: app.startSystemMove(); onDoubleClicked: app.maximize() }
            Row {
                x: sidebar.x + 20; y: sidebar.y + 22; spacing: 8
                Repeater {
                    model: ["#ff5f57", "#febc2e", "#28c840"]
                    AbstractButton {
                        InteractionFeedback {}
                        required property string modelData
                        required property int index
                        width: 12; height: 12; hoverEnabled: true
                        objectName: ["windowClose", "windowMinimize", "windowMaximize"][index]
                        Accessible.name: ["Закрыть", "Свернуть", "Развернуть"][index]
                        background: Rectangle { radius: 6; color: parent.modelData; border.width: 0.6; border.color: Qt.darker(color, 1.12) }
                        contentItem: Txt { text: parent.hovered ? ["×", "−", "+"][parent.index] : ""; color: "#704436"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        onClicked: index === 0 ? app.close() : index === 1 ? app.showMinimized() : app.maximize()
                    }
                }
            }
            Txt {
                x: sidebar.x + sidebar.width + app.gutter; anchors.verticalCenter: parent.verticalCenter; visible: sc.signedIn
                width: Math.max(80, parent.width - x - app.inspectorWidth - 330)
                text: "SCrip"
                color: app.muted
                font.pixelSize: 14; font.weight: Font.DemiBold
            }
            Row {
                visible: sc.signedIn; anchors.right: parent.right; anchors.rightMargin: app.inspectorWidth + app.gutter; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                Tool { symbol: prefs.gridView ? "list" : "grid"; text: prefs.gridView ? "Показать списком" : "Показать обложки"; visible: (app.section !== 4 && app.section !== 5); onClicked: prefs.gridView = !prefs.gridView }
                Rectangle {
                    width: app.width < 1200 ? 195 : 240; height: 30; radius: 15; color: (app.dark ? "#242424" : "#f6f5f6"); border.color: "transparent";
                    Glyph { x: 10; anchors.verticalCenter: parent.verticalCenter; width: 14; height: 14; name: "search"; ink: (app.dark ? "#aaaaaa" : "#8e898e") }
                    Basic.TextField {
                        id: searchField; verticalAlignment: TextInput.AlignVCenter; x: 29; width: parent.width - 36; height: parent.height
                        background: null; leftPadding: 0; topPadding: 0; bottomPadding: 0; font.pixelSize: 12
                        color: app.ink; placeholderText: "Поиск в SoundCloud"; placeholderTextColor: (app.dark ? "#aaaaaa" : "#99949a"); selectByMouse: true
                        text: app.query; onAccepted: app.searchNow()
                    }
                }
            }
        }
        Rectangle {
            id: detailColumn
            objectName: "inspectorColumn"
            visible: sc.signedIn && app.dockedInspector
            x: parent.width - app.inspectorWidth - 12; y: 12
            width: app.inspectorWidth; height: parent.height - y - player.height - 24
            color: (app.dark || app.themed ? app.panel : "#fafafa"); radius: 28
            border.color: (app.dark ? "#252525" : "#eeecef")

            Inspector { anchors.fill: parent; anchors.margins: app.gutter; anchors.topMargin: 20; onDismiss: app.inspectorOpen = false }
        }
        Item {
            id: musicArea
            objectName: "mainContentArea"
            visible: sc.signedIn
            x: sidebar.x + sidebar.width; y: 57; width: parent.width - x - app.inspectorWidth - 12; height: parent.height - y - 1
            Flickable {
                id: contentScroll
                objectName: "libraryScroll"
                visible: !trackPage.visible
                anchors.fill: parent; anchors.leftMargin: app.gutter; anchors.rightMargin: app.gutter
                // Scroll behind the floating player; reserve space at the end, not in the viewport.
                clip: true; contentWidth: width; contentHeight: pageBody.height + 136
                boundsBehavior: Flickable.StopAtBounds
                flickDeceleration: 1500; maximumFlickVelocity: 3600
                property real wheelTarget: 0
                property bool virtualCollectionHovered: false
                function maybeLoad() {
                    if (prefs.autoLoad && app.section < 4 && app.api.hasMore && !app.api.busy && !app.api.error && contentY + height >= contentHeight - 240) sc.more()
                }
                function maybeLoadVirtual(position, viewportSize, totalSize) {
                    if (totalSize > viewportSize && prefs.autoLoad && app.section < 4 && app.api.hasMore && !app.api.busy && !app.api.error && position + viewportSize >= totalSize - 240) sc.more()
                }
                onContentYChanged: {
                    autoLoadTimer.restart()
                    if (!app.restoringScroll && app.activeScrollKey === app.scrollKey) {
                        const old = app.scrollPositions[app.activeScrollKey] || { inner: 0 }
                        app.scrollPositions[app.activeScrollKey] = { outer: contentY, inner: old.inner }
                    }
                }
                onContentHeightChanged: autoLoadTimer.restart()
                onHeightChanged: autoLoadTimer.restart()
                Timer { id: autoLoadTimer; interval: 160; onTriggered: contentScroll.maybeLoad() }
                readonly property bool paginationReady: app.api.hasMore && !app.api.busy && !app.api.error && prefs.autoLoad
                onPaginationReadyChanged: autoLoadTimer.restart()
                onMovementStarted: wheelMotion.stop()
                SmoothedAnimation { id: wheelMotion; target: contentScroll; property: "contentY"; velocity: -1; duration: Math.min(220, app.motion); reversingMode: SmoothedAnimation.Immediate }
                WheelHandler {
                    enabled: !contentScroll.virtualCollectionHovered
                    acceptedDevices: PointerDevice.Mouse
                    onWheel: event => {
                        if (event.pixelDelta.y !== 0 || event.angleDelta.y === 0) { event.accepted = false; return }
                        const end = Math.max(0, contentScroll.contentHeight - contentScroll.height)
                        const sameDirection = (contentScroll.wheelTarget - contentScroll.contentY) * event.angleDelta.y < 0
                        const base = wheelMotion.running && sameDirection ? contentScroll.wheelTarget : contentScroll.contentY
                        contentScroll.wheelTarget = Math.max(0, Math.min(end, base - event.angleDelta.y / 120 * 176))
                        if (prefs.motion) { wheelMotion.to = contentScroll.wheelTarget; wheelMotion.start() }
                        else contentScroll.contentY = contentScroll.wheelTarget
                        event.accepted = true
                    }
                }
                ScrollBar.vertical: ScrollBar { id: pageBar }

                Column {
                    id: pageBody; width: Math.max(0, contentScroll.width - pageBar.implicitWidth - 4); spacing: app.gutter
                    property real entrance: 0
                    transform: Translate { y: pageBody.entrance }
                    ParallelAnimation {
                        id: pageTransition
                        NumberAnimation { target: pageBody; property: "opacity"; from: 0.88; to: 1; duration: app.motion; easing.type: Easing.OutCubic }
                        NumberAnimation { target: pageBody; property: "entrance"; from: 4; to: 0; duration: app.motion; easing.type: Easing.OutCubic }
                    }
                    Item { width: 1; height: 8 }
                    CapsuleSection {
                        visible: app.section !== 3
                        width: parent.width
                        contentItem: Column {
                        width: parent.width; spacing: 8
                        Txt { width: parent.width; text: app.pageTitle; font.pixelSize: 30; font.weight: Font.DemiBold; font.letterSpacing: -0.7 }
                        Txt { visible: app.section !== 4; width: parent.width; color: app.muted; text: app.section === 5 ? "Музыка из ваших подписок и новые открытия" : app.section === 0 ? (app.query ? "Результаты поиска · " + app.query : "Музыка и исполнители SoundCloud") : app.api.tracks.length + " в подборке"; font.pixelSize: 12 }
                    }
                    }
                    Loader { width: parent.width; active: app.section === 3; visible: active; sourceComponent: profileHeader }
                    Rectangle {
                        visible: !!app.api.error; width: parent.width; height: errorLabel.implicitHeight + 24
                        radius: 8; color: "#fff1f2"
                        Txt { id: errorLabel; x: 12; y: 12; width: parent.width - 52; wrapMode: Text.WordWrap; elide: Text.ElideNone; text: app.api.error || ""; color: "#b44150"; font.pixelSize: 12 }
                        Tool { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; symbol: "close"; text: "Закрыть сообщение"; onClicked: sc.clearError() }
                    }
                    CapsuleSection {
                        width: parent.width
                        visible: app.section >= 0 && app.section <= 3
                        padding: 12
                        contentItem: RowLayout {
                        width: parent.width; spacing: 8
                        Tool { visible: app.section === 0; text: "Треки"; active: app.category === 0; onClicked: app.category = 0 }
                        Tool { visible: app.section === 0; text: "Плейлисты"; active: app.category === 1; onClicked: app.category = 1 }
                        Tool { visible: app.section === 0; text: "Профили"; active: app.category === 2; onClicked: app.category = 2 }
                        Tool { visible: app.section === 2; text: "Создать плейлист"; onClicked: playlistDialog.open() }
                        Field {
                            visible: app.section !== 0
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80
                            Layout.preferredWidth: 280
                            placeholderText: app.section === 2 || (app.section === 3 && app.profileTab === 1) ? "Поиск плейлистов" : "Поиск по названию или исполнителю"
                            Accessible.name: placeholderText
                            text: app.collectionQuery
                            onTextEdited: app.collectionQuery = text
                        }
                        Tool {
                            visible: app.section === 1 || (app.section === 3 && app.profileTab !== 1)
                            symbol: "shuffle"; text: "Перемешать"
                            enabled: app.playableCollection.length > 0
                            onClicked: {
                                sc.setShuffle(true)
                                sc.playMix(app.playableCollection, Math.floor(Math.random() * app.playableCollection.length))
                            }
                        }
                        Tool { text: "Обновить"; onClicked: app.refreshPage(true) }
                    }
                    }
                    Loader { width: parent.width; active: app.section === 4; visible: active; sourceComponent: app.settingGroup === 1 ? pluginsPage : settingsPage }
                    PluginPanels { dark: app.dark; ink: app.ink; muted: app.muted;

                        width: pageBody.width
                        manager: app.extensions
                        slotName: app.section === 5 ? "home" : app.section === 3 ? "profile" : "-"
                        onReport: error => app.pluginMessage = error
                    }
                    Label { width: pageBody.width; text: app.pluginMessage; textFormat: Text.PlainText; visible: !!text; wrapMode: Text.Wrap }
                    Loader { width: parent.width; active: app.section === 5; visible: active; sourceComponent: homePage }
                    Loader { id: collectionLoader; width: parent.width; active: (app.section !== 4 && app.section !== 5); visible: active; sourceComponent: app.section === 0 && app.category === 2 ? userResults : prefs.gridView ? coverGrid : trackList }
                    Txt {
                        visible: app.section >= 1 && app.section <= 3 && app.api.tracks.length > 0 && app.filteredCollection.length === 0
                        width: parent.width; text: "Ничего не найдено"; color: app.muted
                        horizontalAlignment: Text.AlignHCenter
                    }
                    BusyIndicator { visible: app.api.busy && (app.section !== 4 && app.section !== 5); running: visible; width: 32; height: 32; anchors.horizontalCenter: parent.horizontalCenter }
                    Column {
                        visible: (app.section !== 4 && app.section !== 5) && !app.api.busy && app.api.tracks.length === 0
                        width: parent.width; topPadding: 90; spacing: 12
                        Glyph { name: app.section === 0 ? "search" : app.section === 2 ? "grid" : "music"; width: 42; height: 42; ink: (app.dark ? "#777777" : "#c5c0c5"); anchors.horizontalCenter: parent.horizontalCenter }
                        Txt { width: parent.width; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 19; font.weight: Font.DemiBold; text: app.section === 0 ? "Найдите любимую музыку" : app.section === 2 ? "Ваши плейлисты" : "Пока нет треков" }
                        Txt { width: parent.width; horizontalAlignment: Text.AlignHCenter; color: app.muted; text: app.section === 0 ? "Введите название трека или исполнителя в поиске." : app.section === 2 ? "Создайте первый плейлист — он появится и в SoundCloud." : "Здесь появится музыка из вашей коллекции SoundCloud."; wrapMode: Text.WordWrap }
                    }
                    Tool { visible: app.api.hasMore && app.section < 4 && (!prefs.autoLoad || !!app.api.error); text: app.api.error ? "Повторить загрузку" : "Загрузить ещё"; enabled: !app.api.busy; anchors.horizontalCenter: parent.horizontalCenter; onClicked: { sc.clearError(); sc.more() } }
                }
            }
            Rectangle {
                id: player
                objectName: "playbackDock"
                z: 2
                readonly property bool hasTrack: !!app.api.current.urn || !!app.api.current.id
                readonly property bool narrow: width < 790
                x: 12; anchors.bottom: parent.bottom; anchors.bottomMargin: 11
                width: shell.width - musicArea.x - 24; height: 84
                color: (app.dark || app.themed ? app.panel : "#fcfcfd"); radius: 28
                border.color: (app.dark ? "#252525" : "#eeecef")

                // Accept unused pointer events before they can reach the scrolling tracks.
                MouseArea {
                    objectName: "playbackDockBarrier"
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    onWheel: event => event.accepted = true
                }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: player.narrow ? 14 : 24
                    anchors.rightMargin: player.narrow ? 14 : 24; spacing: player.narrow ? 8 : 16
                    Rectangle {
                        Layout.preferredWidth: 50; Layout.preferredHeight: 50
                        radius: app.artworkRadius; color: Qt.tint(app.surface, Qt.alpha(app.accent, 0.09))
                        Artwork { radius: app.artworkRadius; id: dockArt; anchors.fill: parent; source: app.cover(app.api.current); visible: !!source }
                        TapHandler { enabled: player.hasTrack; onTapped: app.openTrack(app.api.current, true) }
                        Glyph { visible: !dockArt.visible; anchors.centerIn: parent; width: 24; height: 24; name: "music"; ink: app.accent }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.preferredWidth: 175; Layout.minimumWidth: 52; spacing: 3
                        Txt { objectName: "dockTrackTitle"; Layout.fillWidth: true; text: app.api.current.title || "Выберите трек"; font.pixelSize: 14; font.weight: Font.DemiBold; TapHandler { enabled: player.hasTrack; onTapped: app.openTrack(app.api.current, true) } }
                        Txt { Layout.fillWidth: true; visible: !!text; text: app.api.buffering ? "Загрузка…" : app.api.current.user?.username || ""; font.pixelSize: 12; color: app.muted }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.preferredWidth: 310; Layout.maximumWidth: 460; spacing: 0
                        Row {
                            Layout.alignment: Qt.AlignHCenter; spacing: player.narrow ? 2 : 6
                            Tool { anchors.verticalCenter: parent.verticalCenter; symbol: "shuffle"; active: app.api.shuffle; text: "Перемешать"; onClicked: sc.setShuffle(!app.api.shuffle) }
                            Tool { anchors.verticalCenter: parent.verticalCenter; symbol: "previous"; text: "Предыдущий трек"; enabled: player.hasTrack; onClicked: sc.previous() }
                            Tool {
                                symbol: app.api.buffering ? "loading" : app.api.playing ? "pause" : "play"; text: app.api.buffering ? "Загрузка" : app.api.playing ? "Пауза" : "Воспроизвести"
                                implicitWidth: 38; implicitHeight: 38; primary: true; round: true
                                enabled: player.hasTrack; opacity: 1; onClicked: sc.togglePlayback()
                                background: Rectangle { radius: width / 2; color: parent.enabled ? app.accent : "#a7a7af" }
                                BusyIndicator { anchors.centerIn: parent; width: 28; height: 28; running: !!app.api.buffering; visible: running }
                            }
                            Tool { anchors.verticalCenter: parent.verticalCenter; symbol: "next"; text: "Следующий трек"; enabled: !!app.api.queue?.length; onClicked: sc.next() }
                            Tool { anchors.verticalCenter: parent.verticalCenter; symbol: "repeat"; active: app.api.repeat; text: "Повтор трека"; onClicked: sc.setRepeat(!app.api.repeat) }
                        }
                        RowLayout {
                            visible: player.hasTrack; Layout.fillWidth: true; spacing: 8
                            Txt { text: app.time(sc.position); color: app.muted; font.pixelSize: 10 }
                            LineSlider {
                                id: trackProgress
                                Layout.fillWidth: true; implicitHeight: 17; from: 0; to: Math.max(1, sc.duration || 1)
                                property bool adjusting: false
                                Binding { target: trackProgress; property: "value"; value: sc.position || 0; when: !trackProgress.adjusting; restoreMode: Binding.RestoreNone }
                                onPressedChanged: {
                                    if (pressed) adjusting = true
                                    else { sc.seek(value); adjusting = false }
                                }
                                onMoved: if (!pressed) sc.seek(value)
                                Accessible.name: "Позиция трека"
                            }
                            Txt { text: app.time(sc.duration); color: app.muted; font.pixelSize: 10 }
                        }
                    }
                    Item { Layout.fillWidth: true; Layout.preferredWidth: player.narrow ? 0 : 48 }
                    LikeButton { visible: player.hasTrack; track: app.api.current }
                    RowLayout {
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        spacing: 12
                        Tool { text: "Дополнения"; visible: !!app.extensions && app.extensions.panels.some(p => p.page === "player"); onClicked: pluginTools.open() }
                        Tool { symbol: "list"; text: "Очередь воспроизведения"; active: app.dockedInspector || queuePopup.opened; onClicked: app.toggleInspector() }
                        Glyph { visible: !player.narrow; name: "volume"; Layout.preferredWidth: 18; Layout.preferredHeight: 18; ink: app.muted }
                        VolumeSlider { visible: !player.narrow; Layout.minimumWidth: 96; Layout.preferredWidth: 96; Layout.maximumWidth: 96 }
                    }
                }
            }
        }

        Column {
            objectName: "welcomeScreen"
            visible: !sc.signedIn
            anchors.centerIn: parent; width: Math.min(460, parent.width - 64); spacing: 16
            BrandMark { anchors.horizontalCenter: parent.horizontalCenter; width: 144; height: width }
            Txt {
                width: parent.width; text: "Добро пожаловать в SCrip"
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; elide: Text.ElideNone
                font.pixelSize: 30; font.weight: Font.DemiBold; font.letterSpacing: -0.9
            }
            Txt {
                width: parent.width; text: "Войдите, чтобы слушать свои треки и плейлисты."
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; color: app.muted
            }
            Tool {
                anchors.horizontalCenter: parent.horizontalCenter; width: 222; height: 44; primary: true
                text: app.api.authStep === "exchange" ? "Завершаем вход…" : app.api.authBusy ? "Окно входа открыто" : "Войти в SoundCloud"
                enabled: !app.api.authBusy; onClicked: sc.login()
            }
            Txt { visible: app.api.authStep === "browser"; width: parent.width; text: "Завершите вход в отдельном окне.\nSCrip откроет медиатеку автоматически."; horizontalAlignment: Text.AlignHCenter; color: app.muted; wrapMode: Text.WordWrap; lineHeight: 1.4 }
            Txt { visible: !!app.api.error; width: parent.width; text: app.api.error || ""; color: "#bc3b4a"; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter; spacing: 8
                Tool { visible: app.api.authStep === "browser" || app.api.authStep === "exchange"; text: "Отменить"; onClicked: sc.cancelLogin() }
            }
        }
    }
    Popup {
        id: trackPage
        opacity: 0; scale: 0.985
        enter: SurfaceTransition { duration: app.motion }
        exit: SurfaceTransition { duration: app.motion; leaving: true }
        objectName: "trackPage"
        parent: musicArea
        x: app.gutter; y: 0
        width: musicArea.width - app.gutter * 2
        height: Math.max(1, musicArea.height - player.height - 28)
        modal: false; dim: false; focus: true
        closePolicy: Popup.CloseOnEscape
        padding: width < 560 ? 16 : 24
        property var selectedTrack: ({})
        property bool followCurrent: false
        property bool changingSelection: false
        readonly property bool isCurrent: !!app.api.current.urn && (followCurrent || selectedTrack.urn === app.api.current.urn)
        readonly property var track: followCurrent || isCurrent ? app.api.current : selectedTrack
        readonly property string trackKey: String(track.urn || track.id || "")
        onTrackKeyChanged: if (opened && !changingSelection) loadLyrics()
        readonly property var lyrics: typeof lyricsBackend !== "undefined" ? lyricsBackend : null
        readonly property var lyricState: lyrics ? lyrics.state : ({})
        property bool showDescription: false
        property bool followLyrics: true
        readonly property int lyricIndex: {
            if (!isCurrent || !lyricState.synced) return -1
            const lines = lyricState.lines || []
            let low = 0, high = lines.length
            while (low < high) {
                const mid = Math.floor((low + high) / 2)
                if (lines[mid].time <= sc.position) low = mid + 1
                else high = mid
            }
            return low - 1
        }
        function loadLyrics() {
            detailsScroll.contentY = 0
            showDescription = false; followLyrics = true
            customLyrics.close()
            if (lyrics) lyrics.lookup(track.title || "", track.user?.username || "", track.duration || 0, trackKey)
        }
        function playLyric(time) {
            if (!lyricState.synced || time < 0) return
            const target = track
            if (!isCurrent) sc.play(target)
            sc.seek(time)
            sc.resumePlayback()
            followLyrics = true
            Qt.callLater(function() { lyricList.centerActive(true) })
        }
        function updateSpectrumActivity() { sc.setTrackVisualActive(opened && isCurrent && app.visible && app.visibility !== Window.Minimized) }
        onOpened: { updateSpectrumActivity(); loadLyrics() }
        onClosed: { updateSpectrumActivity(); if (lyrics) lyrics.cancel() }
        onIsCurrentChanged: updateSpectrumActivity()
        Connections { target: app; function onVisibleChanged() { trackPage.updateSpectrumActivity() } function onVisibilityChanged() { trackPage.updateSpectrumActivity() } }
        Dialog {
            id: customLyrics
            opacity: 0; scale: 0.985
        enter: SurfaceTransition { duration: app.motion }
            exit: SurfaceTransition { duration: app.motion; leaving: true }
            objectName: "customLyricsDialog"
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(560, app.width - 40)
            height: Math.min(500, app.height - 64)
            modal: true; title: "Добавить текст песни"
            standardButtons: Dialog.Save | Dialog.Cancel
            onAccepted: trackPage.lyrics.setCustomText(customLyricText.text)
            onOpened: { standardButton(Dialog.Save).enabled = customLyricText.text.trim().length > 0; customLyricText.forceActiveFocus() }
            contentItem: ColumnLayout {
                spacing: 12
                Txt { Layout.fillWidth: true; text: "Вставьте обычный текст или LRC: [00:12.50]Строка песни. Для подсветки и переходов нужны таймкоды. Текст сохранится на этом устройстве."; wrapMode: Text.WordWrap; color: app.muted }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    TextArea {
                        id: customLyricText
                        objectName: "customLyricText"
                        wrapMode: TextEdit.Wrap; selectByMouse: true
                        color: app.ink
                        placeholderText: "[00:12.50]Первая строка\n[00:18.00]Вторая строка"
                        onTextChanged: if (customLyrics.opened) customLyrics.standardButton(Dialog.Save).enabled = text.trim().length > 0 && text.length <= 131072
                        background: Rectangle { radius: 10; color: app.surface; border.color: app.line }
                    }
                }
            }
        }
        background: Rectangle { radius: 28; color: app.surface; border.color: app.line }
        contentItem: ColumnLayout {
            spacing: 18
            RowLayout {
                Layout.fillWidth: true
                Tool { objectName: "trackPageBack"; text: "Назад"; onClicked: trackPage.close() }
                Txt { text: "МУЗЫКА В ДЕТАЛЯХ"; color: app.muted; font.pixelSize: 10; font.letterSpacing: 2; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                Tool { symbol: "close"; text: "Закрыть страницу"; onClicked: trackPage.close() }
            }
            Flickable {
                id: detailsScroll
                objectName: "trackDetailsScroll"
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: width; contentHeight: trackDetails.height
                clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                GridLayout {
                    id: trackDetails
                    width: parent.width
                    columns: width >= 660 ? 2 : 1
                    columnSpacing: 20; rowSpacing: 20
                    Column {
                        id: trackMainColumn
                        objectName: "trackMainColumn"
                        Layout.preferredWidth: trackDetails.columns === 2 ? (trackDetails.width - trackDetails.columnSpacing) * 0.66 : trackDetails.width
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        spacing: 20
                        Rectangle {
                            id: trackHero
                            width: parent.width; height: hero.implicitHeight + 48
                            radius: 24
                            color: app.panel
                            GridLayout {
                                id: hero
                                x: 24; y: 24; width: parent.width - 48
                                columns: width >= 760 ? 2 : 1
                                columnSpacing: 32; rowSpacing: 24
                                Item {
                                    Layout.preferredWidth: hero.columns === 2 ? Math.min(380, hero.width * 0.44) : Math.min(320, hero.width * 0.82)
                                    Layout.preferredHeight: width + 10
                                    Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                                    Rectangle { x: 10; y: 10; width: parent.width - 20; height: width; radius: 22; color: Qt.alpha(app.ink, 0.06) }
                                    Rectangle {
                                        width: parent.width; height: width; radius: Math.max(16, app.artworkRadius); color: app.raised
                                        border.color: Qt.alpha(app.ink, 0.06)
                                        Glyph { anchors.centerIn: parent; width: 72; height: 72; name: "music"; ink: app.muted }
                                        Artwork {
                                            anchors.fill: parent; radius: parent.radius
                                            source: app.cover(trackPage.track).replace("-t300x300.", "-t500x500.")
                                            visible: !!source
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignVCenter; spacing: 12
                                    Txt { Layout.alignment: hero.columns === 1 ? Qt.AlignHCenter : Qt.AlignLeft; text: trackPage.isCurrent && app.api.playing ? "СЕЙЧАС ИГРАЕТ" : "SOUNDCLOUD"; color: app.muted; font.pixelSize: 9; font.weight: Font.Medium; font.letterSpacing: 1.8 }
                                    Txt {
                                        objectName: "trackDetailsTitle"
                                        Layout.fillWidth: true; Layout.minimumWidth: 0; text: trackPage.track.title || "Без названия"
                                        font.pixelSize: hero.width > 850 ? 36 : 28; font.weight: Font.Bold
                                        horizontalAlignment: hero.columns === 1 ? Text.AlignHCenter : Text.AlignLeft
                                        wrapMode: Text.Wrap; elide: Text.ElideNone; lineHeight: 1.1
                                    }
                                    AbstractButton {
                                        InteractionFeedback {}
                                        Layout.fillWidth: true
                                        Accessible.name: "Открыть исполнителя"
                                        onClicked: { trackPage.close(); app.openProfile(trackPage.track.user) }
                                        enabled: !!trackPage.track.user?.urn
                                        contentItem: Txt { text: trackPage.track.user?.username || "SoundCloud"; color: app.muted; font.pixelSize: 15; wrapMode: Text.Wrap; horizontalAlignment: hero.columns === 1 ? Text.AlignHCenter : Text.AlignLeft }
                                    }
                                    Txt {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        horizontalAlignment: hero.columns === 1 ? Text.AlignHCenter : Text.AlignLeft
                                        text: [app.time(trackPage.track.duration), trackPage.track.genre || ""].filter(v => !!v).join("   ·   ")
                                        color: app.muted; font.pixelSize: 11
                                    }
                                    Row {
                                        Layout.alignment: hero.columns === 1 ? Qt.AlignHCenter : Qt.AlignLeft
                                        Layout.topMargin: 6
                                        spacing: 12
                                        Tool {
                                            objectName: "trackPagePlay"
                                            primary: true; round: true; implicitHeight: 42; implicitWidth: hero.width < 500 ? 96 : 112
                                            text: trackPage.isCurrent && app.api.playing ? "Пауза" : "Слушать"
                                            onClicked: { if (trackPage.isCurrent) sc.togglePlayback(); else sc.play(trackPage.track) }
                                        }
                                        LikeButton { track: trackPage.track; height: 42; width: hero.width < 500 ? 32 : 42 }
                                        Tool { symbol: "menu"; height: 42; width: hero.width < 500 ? 32 : 42; text: "Действия с треком"; onClicked: app.openTrackActions(trackPage.track) }
                                    }
                                }
                                ColumnLayout {
                                    Layout.columnSpan: hero.columns
                                    Layout.fillWidth: true; spacing: 12
                                    Rectangle { Layout.fillWidth: true; height: 1; color: Qt.alpha(app.ink, 0.08); Layout.topMargin: 4 }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 4
                                        Txt { text: "Звучание"; color: app.muted; font.pixelSize: 11; Layout.fillWidth: true }
                                        Tool { objectName: "spectrumSolid"; text: "Волна"; implicitWidth: 62; round: true; active: prefs.spectrumStyle === 0; onClicked: prefs.spectrumStyle = 0; background: Rectangle { radius: height / 2; color: prefs.spectrumStyle === 0 ? app.surface : "transparent" } }
                                        Tool { objectName: "spectrumBars"; text: "Столбики"; implicitWidth: 88; round: true; active: prefs.spectrumStyle === 1; onClicked: prefs.spectrumStyle = 1; background: Rectangle { radius: height / 2; color: prefs.spectrumStyle === 1 ? app.surface : "transparent" } }
                                    }
                                    Item {
                                        id: spectrumView
                                        Layout.fillWidth: true; Layout.preferredHeight: 72
                                        readonly property var samples: trackPage.isCurrent ? sc.spectrum : []
                                        Canvas {
                                            id: spectrumWave
                                            anchors.fill: parent; visible: prefs.spectrumStyle === 0
                                            property var displayedSamples: []
                                            onWidthChanged: requestPaint()
                                            onHeightChanged: requestPaint()
                                            onVisibleChanged: requestPaint()
                                            Connections {
                                                target: spectrumView
                                                function onSamplesChanged() {
                                                    if (!spectrumBlend.running) { spectrumWave.displayedSamples = spectrumView.samples; spectrumWave.requestPaint() }
                                                }
                                            }
                                            Timer {
                                                id: spectrumBlend
                                                interval: 16; repeat: true
                                                running: prefs.motion && spectrumWave.visible && trackPage.opened && trackPage.isCurrent && app.visible && app.visibility !== Window.Minimized
                                                onTriggered: {
                                                    const next = [], old = spectrumWave.displayedSamples, target = spectrumView.samples
                                                    let moving = old.length !== 96
                                                    for (let i = 0; i < 96; ++i) {
                                                        const a = old[i] || 0, b = target[i] || 0
                                                        next.push(Math.abs(b - a) < 0.001 ? b : a + (b - a) * 0.16)
                                                        if (Math.abs(next[i] - a) > 0.0001) moving = true
                                                    }
                                                    if (moving) { spectrumWave.displayedSamples = next; spectrumWave.requestPaint() }
                                                }
                                            }
                                            Connections { target: app; function onAccentChanged() { spectrumWave.requestPaint() } }
                                            onPaint: {
                                                const c = getContext("2d"); c.reset()
                                                const raw = spectrumWave.displayedSamples, values = [], n = 96, bottom = height - 2
                                                // A small Gaussian-like kernel rounds neighboring frequency peaks.
                                                const weights = [1, 3, 6, 8, 6, 3, 1]
                                                for (let i = 0; i < n; ++i) {
                                                    let sum = 0
                                                    for (let k = -3; k <= 3; ++k) sum += (raw[Math.max(0, Math.min(n - 1, i + k))] || 0) * weights[k + 3]
                                                    values.push(sum / 28)
                                                }
                                                const gradient = c.createLinearGradient(0, 0, 0, height)
                                                gradient.addColorStop(0, Qt.alpha(app.accent, 0.48))
                                                gradient.addColorStop(1, Qt.alpha(app.accent, 0.025))
                                                c.beginPath(); c.moveTo(0, bottom)
                                                for (let i = 0; i < n; ++i) {
                                                    const x = i * width / (n - 1), y = bottom - 2 - (height - 8) * (values[i] || 0)
                                                    if (i === 0) c.lineTo(x, y)
                                                    else {
                                                        const px = (i - 1) * width / (n - 1), py = bottom - 2 - (height - 8) * (values[i - 1] || 0)
                                                        c.bezierCurveTo((px + x) / 2, py, (px + x) / 2, y, x, y)
                                                    }
                                                }
                                                c.lineTo(width, bottom); c.closePath(); c.fillStyle = gradient; c.fill()
                                                c.strokeStyle = Qt.alpha(app.accent, 0.52); c.lineWidth = 1.2; c.stroke()
                                            }
                                        }
                                        Row {
                                            anchors.fill: parent; spacing: Math.min(2, width / 192); visible: prefs.spectrumStyle === 1
                                            Repeater {
                                                model: 96
                                                delegate: Item {
                                                    required property int index
                                                    width: (spectrumView.width - 95 * parent.spacing) / 96; height: parent.height
                                                    Rectangle {
                                                        anchors.bottom: parent.bottom; width: parent.width
                                                        height: 2 + (parent.height - 4) * (spectrumView.samples[index] || 0)
                                                        radius: width / 2; color: Qt.alpha(app.accent, 0.45 + 0.55 * index / 95)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Txt { text: "30 Гц"; font.pixelSize: 9; color: app.muted }
                                        Item { Layout.fillWidth: true }
                                        Txt { text: trackPage.isCurrent && app.api.playing ? "ЖИВОЙ ЗВУК" : "ОЖИДАНИЕ ЗВУКА"; font.pixelSize: 8; font.letterSpacing: 1; color: app.muted }
                                        Item { Layout.fillWidth: true }
                                        Txt { text: "18 кГц"; font.pixelSize: 9; color: app.muted }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            width: parent.width
                            Tool { visible: !!trackPage.track.description; text: trackPage.showDescription ? "Скрыть описание" : "О треке"; onClicked: trackPage.showDescription = !trackPage.showDescription }
                            Item { Layout.fillWidth: true }
                            Tool { visible: !!trackPage.track.permalink_url; text: "SoundCloud ↗"; onClicked: sc.openExternal(trackPage.track.permalink_url) }
                        }
                        CapsuleSection {
                            width: parent.width; visible: trackPage.showDescription && !!trackPage.track.description
                            Txt { width: parent.width; text: trackPage.track.description || ""; wrapMode: Text.Wrap; elide: Text.ElideNone; lineHeight: 1.5; color: app.muted }
                        }
                    }
                    CapsuleSection {
                        id: lyricsPane
                        objectName: "trackLyricsPane"
                        Layout.preferredWidth: trackDetails.columns === 2 ? (trackDetails.width - trackDetails.columnSpacing) * 0.34 : trackDetails.width
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        Layout.minimumHeight: trackDetails.columns === 2 ? trackHero.height : 400
                        padding: width < 360 ? 20 : 28
                        Column {
                            id: lyricsHeading
                            width: parent.width; spacing: 10
                            RowLayout {
                                width: parent.width; spacing: 4
                                Txt { Layout.fillWidth: true; text: "Текст песни"; font.pixelSize: lyricsPane.width < 300 ? 20 : 24; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                                Tool {
                                    text: "⋯"; implicitWidth: 28
                                    Accessible.name: "Действия с текстом"
                                    onClicked: lyricsMenu.open()
                                    Menu {
                                        id: lyricsMenu
                                        MenuItem { InteractionFeedback {} objectName: "editLyrics"; text: trackPage.lyricState.custom ? "Изменить свой текст" : "Добавить свой текст"; onClicked: { customLyricText.text = trackPage.lyricState.custom ? trackPage.lyricState.rawText : ""; customLyrics.open() } }
                                        MenuItem { InteractionFeedback {} visible: !!trackPage.lyricState.custom; height: visible ? implicitHeight : 0; text: "Удалить свой текст"; onTriggered: trackPage.lyrics.removeCustomText() }
                                    }
                                }
                            }
                            Tool { objectName: "followLyrics"; text: "Следить"; visible: !!trackPage.lyricState.synced; active: trackPage.followLyrics; onClicked: { trackPage.followLyrics = !trackPage.followLyrics; if (trackPage.followLyrics) lyricList.centerActive(true) } }
                        }
                        Item {
                            width: parent.width
                            height: visible ? Math.max(260, trackHero.height - lyricsHeading.height - 96) : 0
                            visible: !lyricList.count
                            Column {
                                anchors.centerIn: parent; width: parent.width; spacing: 16
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 56; height: 56; radius: 28; color: Qt.alpha(app.accent, 0.07)
                                    Glyph { anchors.centerIn: parent; name: "music"; width: 24; height: 24; ink: app.accent }
                                }
                                Txt {
                                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                                    text: trackPage.lyricState.busy ? "Ищем текст…" : trackPage.lyricState.instrumental ? "Только музыка" : "Текст не найден"
                                    font.pixelSize: 18; font.weight: Font.DemiBold; wrapMode: Text.Wrap
                                }
                                Txt {
                                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                                    visible: !!trackPage.lyricState.busy || !!trackPage.lyricState.instrumental
                                    text: trackPage.lyricState.busy ? "Это займёт немного времени" : "У этого трека нет вокала"
                                    color: app.muted; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.4
                                }
                                Tool { anchors.horizontalCenter: parent.horizontalCenter; visible: !trackPage.lyricState.busy; text: "Повторить"; round: true; onClicked: trackPage.loadLyrics() }
                            }
                        }
                        ListView {
                            id: lyricList; objectName: "lyricsList"
                            width: parent.width; height: visible ? (trackDetails.columns === 2 ? Math.max(280, detailsScroll.height - 230) : 320) : 0
                            visible: count > 0
                            model: trackPage.lyricState.lines || []
                            currentIndex: trackPage.lyricIndex
                            spacing: trackPage.lyricState.synced ? 4 : 0; clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            topMargin: 12; bottomMargin: 16
                            function centerActive(animate) {
                                if (currentIndex < 0 || !visible) return
                                lyricScroll.stop()
                                const previous = contentY
                                positionViewAtIndex(currentIndex, ListView.Center)
                                const target = contentY
                                if (animate && app.motion > 0 && Math.abs(target - previous) > 1) {
                                    contentY = previous
                                    lyricScroll.from = previous; lyricScroll.to = target
                                    lyricScroll.restart()
                                }
                            }
                            NumberAnimation {
                                id: lyricScroll
                                target: lyricList; property: "contentY"
                                duration: app.motion; easing.type: Easing.InOutCubic
                            }
                            onMovementStarted: { lyricScroll.stop(); trackPage.followLyrics = false }
                            onCurrentIndexChanged: if (trackPage.followLyrics) centerActive(true)
                            onModelChanged: { lyricScroll.stop(); Qt.callLater(function() { if (trackPage.followLyrics) lyricList.centerActive(false) }) }
                            ScrollBar.vertical: ScrollBar {}
                            delegate: AbstractButton {
                                InteractionFeedback {}
                                id: lyricLine
                                objectName: "lyricLine" + index
                                required property var modelData
                                required property int index
                                readonly property bool highlighted: index === trackPage.lyricIndex
                                readonly property bool sectionLabel: /^\[[^\]]+\]$/.test(modelData.text || "")
                                x: 4; width: lyricList.width - 8
                                height: lyricText.implicitHeight + (trackPage.lyricState.synced ? 12 : 4) + (sectionLabel ? 8 : 0)
                                leftPadding: 8; rightPadding: 8
                                topPadding: (trackPage.lyricState.synced ? 6 : 2) + (sectionLabel ? 8 : 0)
                                bottomPadding: trackPage.lyricState.synced ? 6 : 2
                                hoverEnabled: true
                                enabled: !!trackPage.lyricState.synced && !!trackPage.track.urn
                                Accessible.description: "Воспроизвести с " + app.time(modelData.time)
                                background: Item {
                                    Rectangle {
                                        anchors.fill: parent; radius: 12
                                        color: lyricLine.hovered ? Qt.alpha(app.ink, 0.035) : "transparent"
                                        Behavior on color { ColorAnimation { duration: app.motion } }
                                    }
                                    Rectangle {
                                        objectName: "lyricGlow" + lyricLine.index
                                        anchors.fill: parent; radius: 12
                                        color: Qt.alpha(app.accent, 0.035)
                                        opacity: lyricLine.highlighted ? 1 : 0
                                        layer.enabled: opacity > 0
                                        layer.effect: MultiEffect {
                                            shadowEnabled: true; shadowColor: app.accent
                                            shadowOpacity: 0.16; shadowBlur: 0.8; blurMax: 12
                                            shadowHorizontalOffset: 0; shadowVerticalOffset: 0
                                        }
                                        Behavior on opacity { NumberAnimation { duration: app.motion; easing.type: Easing.InOutQuad } }
                                        Behavior on color { ColorAnimation { duration: app.motion } }
                                    }
                                    Rectangle {
                                        x: 0; y: 6; width: 2; height: parent.height - 12; radius: 1
                                        color: app.accent; opacity: lyricLine.highlighted ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: app.motion } }
                                        Behavior on color { ColorAnimation { duration: app.motion } }
                                    }
                                }
                                Accessible.name: modelData.text || "Музыкальная пауза"
                                onClicked: trackPage.playLyric(modelData.time)
                                contentItem: Txt {
                                    id: lyricText
                                    text: lyricLine.modelData.text || "♪ ♪ ♪"
                                    wrapMode: Text.WordWrap; elide: Text.ElideNone
                                    font.pixelSize: lyricLine.sectionLabel ? 12 : lyricsPane.width < 300 ? 15 : 16
                                    font.weight: Font.Normal; lineHeight: 1.25
                                    color: lyricLine.highlighted ? app.accent : lyricLine.sectionLabel ? app.muted : app.ink
                                    opacity: !trackPage.lyricState.synced || !trackPage.isCurrent ? 0.85 : lyricLine.highlighted || lyricLine.hovered ? 1 : 0.45
                                    Behavior on color { ColorAnimation { duration: app.motion } }
                                    Behavior on opacity { NumberAnimation { duration: app.motion } }
                                }
                            }
                        }
                        Column {
                            width: parent.width; spacing: 8
                            Txt { width: parent.width; visible: lyricList.count > 0; text: trackPage.lyricState.synced ? "Нажмите на строку для перехода" : "Текст без таймкодов"; wrapMode: Text.WordWrap; font.pixelSize: 10; color: app.muted }
                            AbstractButton {
                                InteractionFeedback {}
                                visible: !trackPage.lyricState.custom && lyricList.count > 0
                                implicitHeight: 24; implicitWidth: credit.implicitWidth
                                Accessible.name: "Источник текста — " + (trackPage.lyricState.source || "LRCLIB")
                                onClicked: sc.openExternal(trackPage.lyricState.sourceUrl || "https://lrclib.net")
                                contentItem: Txt { id: credit; text: "Текст: " + (trackPage.lyricState.source || "LRCLIB") + " ↗"; color: app.muted; font.pixelSize: 10 }
                            }
                            Txt { width: parent.width; visible: !!trackPage.lyricState.custom; text: "Ваш текст · На этом устройстве"; font.pixelSize: 10; color: app.muted }
                        }
                    }

                }
            }
        }
    }
    Component {
        id: homePage
        Column {
            width: parent.width; spacing: app.gutter
            readonly property var mixes: app.api.home.filter(s => s.mix)
            WavePanel {
                width: parent.width
                backend: sc
                dark: app.dark
                panelColor: app.panel
                inkColor: app.ink
                waveColor: app.accent
                motion: prefs.motion
                foreground: app.visible && app.visibility !== Window.Minimized
                    && (Qt.platform.os !== "android" || Qt.application.state === Qt.ApplicationActive)
            }
            CapsuleSection {
                visible: parent.mixes.length > 0; width: parent.width
                contentItem: Txt { text: "Ваши миксы"; font.pixelSize: 21; font.weight: Font.DemiBold }
            }
            Repeater {
                model: parent.mixes
                delegate: CapsuleSection {
                    id: mix
                    required property var modelData
                    readonly property string storageKey: (app.api.profile.urn || "") + "/" + modelData.title
                    readonly property var hidden: JSON.parse(prefs.hiddenMixTracks)[storageKey] || []
                    readonly property var songs: modelData.tracks.filter(t => !hidden.includes(t.urn))
                    width: parent.width
                    contentItem: RowLayout {
                        spacing: 16
                    Column {
                        Layout.preferredWidth: Math.min(184, mix.width * 0.28); spacing: 5
                        Artwork { radius: app.artworkRadius;
                            width: parent.width; height: width; source: app.cover(mix.modelData.tracks[0])
                            TapHandler { onTapped: playlistView.beginMix(mix.modelData, mix.songs) }
                        }
                        Txt { width: parent.width; text: mix.modelData.title; font.weight: Font.DemiBold; TapHandler { onTapped: playlistView.beginMix(mix.modelData, mix.songs) } }
                        Txt { text: mix.songs.length + " в подборке"; color: app.muted }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                        ListView {
                            id: mixList
                            Layout.fillWidth: true; Layout.preferredHeight: Math.min(5, count) * 42
                            clip: true; model: mix.songs
                            ScrollBar.vertical: ScrollBar { id: mixBar }
                            delegate: RowLayout {
                                id: song
                                required property var modelData
                                required property int index
                                width: mixList.width - mixBar.implicitWidth; height: 42; spacing: 2
                                AbstractButton {
                                    InteractionFeedback {}
                                    Layout.fillWidth: true; Layout.minimumWidth: 0
                                    Accessible.name: song.modelData.title
                                    onClicked: app.openTrack(song.modelData)
                                    contentItem: Column {
                                        Txt { width: parent.width; text: song.modelData.title || "Трек"; font.weight: Font.Medium }
                                        Txt { width: parent.width; text: song.modelData.user?.username || ""; color: app.muted; font.pixelSize: 11 }
                                    }
                                }
                                Tool { symbol: "play"; text: "Слушать"; onClicked: sc.playMix(mix.songs, song.index) }
                                LikeButton { track: song.modelData }
                                Tool { symbol: "menu"; text: "Действия с треком"; onClicked: app.openTrackActions(song.modelData) }
                                Tool { symbol: "close"; text: "Убрать из микса"; onClicked: {
                                    const all = JSON.parse(prefs.hiddenMixTracks)
                                    all[mix.storageKey] = mix.hidden.concat([song.modelData.urn])
                                    prefs.hiddenMixTracks = JSON.stringify(all)
                                } }
                            }
                        }
                        Tool { visible: mix.hidden.length > 0; text: "Вернуть скрытые"; onClicked: {
                            const all = JSON.parse(prefs.hiddenMixTracks)
                            delete all[mix.storageKey]
                            prefs.hiddenMixTracks = JSON.stringify(all)
                        } }
                    }
                                    }
                }
            }
            Repeater {
                model: {
                    const shelves = app.api.home
                    const artists = new Set()
                    const seeds = []
                    for (const shelf of shelves) for (const track of shelf.tracks) {
                        const id = track.user?.urn
                        if (id && !artists.has(id) && seeds.length < 16) { artists.add(id); seeds.push(track) }
                    }
                    return (seeds.length ? [{title: "Станции исполнителей", station: true, tracks: seeds}] : []).concat(shelves.filter(s => !s.mix))
                }
                delegate: CapsuleSection {
                    id: shelf
                    required property var modelData
                    width: parent.width
                    contentItem: Column {
                        spacing: 12
                    RowLayout {
                        width: parent.width; spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Txt { Layout.fillWidth: true; text: shelf.modelData.title; font.pixelSize: 21; font.weight: Font.DemiBold; TapHandler { enabled: !shelf.modelData.station; onTapped: playlistView.beginMix(shelf.modelData, shelf.modelData.tracks) } }
                            Txt { Layout.fillWidth: true; visible: !!shelf.modelData.subtitle; text: shelf.modelData.subtitle || ""; color: app.muted }
                        }
                        Tool { visible: cards.contentWidth > cards.width; enabled: !cards.atXBeginning; text: "Прокрутить влево"; symbol: "back"; onClicked: cards.stepPage(-1) }
                        Tool { visible: cards.contentWidth > cards.width; enabled: !cards.atXEnd; text: "Прокрутить вправо"; symbol: "arrow"; onClicked: cards.stepPage(1) }
                    }
                    ListView {
                        id: cards
                        width: parent.width; height: cardHeight + shelfBar.implicitHeight + 4
                        function stepPage(direction) {
                            cancelFlick()
                            const start = shelfMotion.running ? shelfMotion.to : contentX
                            shelfMotion.stop()
                            const end = Math.max(originX, Math.min(originX + contentWidth - width, start + direction * (cardWidth + spacing)))
                            if (!app.motion) { contentX = end; return }
                            shelfMotion.to = end
                            shelfMotion.start()
                        }
                        onMovementStarted: shelfMotion.stop()
                        NumberAnimation { id: shelfMotion; target: cards; property: "contentX"; duration: app.motion; easing.type: Easing.OutCubic }
                        readonly property real cardHeight: cardWidth + 52
                        readonly property int cardWidth: Math.floor((width - (slots - 1) * spacing) / slots)
                        readonly property int slots: Math.max(2, Math.floor((width + spacing) / 170))
                        orientation: ListView.Horizontal; spacing: 16; clip: true
                        model: shelf.modelData.tracks
                        ScrollBar.horizontal: ScrollBar { id: shelfBar }
                        delegate: AbstractButton {
                            InteractionFeedback {}
                            id: card
                            objectName: shelf.modelData.station ? "artistStationCard" + index : "homeTrackCard" + index
                            required property var modelData
                            required property int index
                            width: cards.cardWidth; height: cards.cardHeight
                            Accessible.name: modelData.title || "Слушать"
                            onClicked: shelf.modelData.station ? playlistView.beginStation(modelData) : app.openTrack(modelData)
                            background: Item {}
                            contentItem: Column {
                                spacing: 4
                                Artwork { width: card.width; height: width; radius: shelf.modelData.station ? width / 2 : app.artworkRadius; source: shelf.modelData.station ? app.imageUrl(card.modelData.user?.avatar_url) || app.cover(card.modelData) : app.cover(card.modelData) }
                                Txt { width: card.width; text: shelf.modelData.station ? card.modelData.user.username : card.modelData.title || "Без названия"; font.weight: Font.Medium }
                                Txt { width: card.width; text: shelf.modelData.station ? "Станция исполнителя" : card.modelData.user?.username || ""; color: app.muted; font.pixelSize: 12 }
                            }
                            scale: pressed && prefs.motion ? 0.975 : 1
                            Behavior on scale { NumberAnimation { duration: app.motion / 2; easing.type: Easing.OutCubic } }
                        }
                    }
                                    }
                }
            }
            BusyIndicator { visible: app.api.homeBusy; running: visible; width: 32; height: 32 }
            Txt { visible: !!app.api.homeError; width: parent.width; text: app.api.homeError; color: app.muted; wrapMode: Text.WordWrap }
            Txt { visible: !app.api.homeBusy && !app.api.homeError && !app.api.home.length; width: parent.width; text: "Добавьте любимые треки или подпишитесь на исполнителей, чтобы здесь появились подборки."; wrapMode: Text.WordWrap }
            Tool { text: "Обновить главную"; enabled: !app.api.homeBusy; onClicked: sc.loadHome(true) }
        }
    }
    Component {
        id: userResults
        Column {
            spacing: 6
            Repeater {
                model: app.api.tracks
                delegate: AbstractButton {
                    InteractionFeedback {}
                    id: person
                    required property var modelData
                    width: parent.width; height: 58
                    Accessible.name: modelData.username || "Профиль"
                    onClicked: app.openProfile(modelData)
                    background: Rectangle { radius: 16; color: person.hovered ? Qt.alpha(app.accent, 0.10) : "transparent"; Behavior on color { ColorAnimation { duration: app.motion * 0.6 } } }
                    leftPadding: 12; rightPadding: 12
                    contentItem: RowLayout {
                        spacing: 12
                        Avatar { Layout.preferredWidth: 44; Layout.preferredHeight: 44; source: app.imageUrl(person.modelData.avatar_url) }
                        Txt { Layout.fillWidth: true; text: person.modelData.username || "Профиль"; font.weight: Font.Medium }
                        Glyph { name: "arrow"; width: 18; height: 18; ink: app.muted }
                    }
                }
            }
        }
    }
    Component {
        id: coverGrid
        GridView {
            id: virtualCoverGrid
            objectName: "coverGridView"
            width: parent.width
            property int columns: Math.max(2, Math.floor((width + 24) / 184))
            property real tileWidth: Math.max(0, width / columns - 24)
            property real tileHeight: tileWidth + 48
            cellWidth: width / columns
            cellHeight: tileHeight + 24
            height: Math.min(count > 0 ? Math.ceil(count / columns) * cellHeight - 24 : 0, Math.max(58, contentScroll.height - y - 110))
            clip: true
            cacheBuffer: Math.max(cellHeight * 2, height / 2)
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            model: app.collectionQuery.trim() ? app.filteredCollection : sc.trackModel
            property int activeDelegateCount: 0
            function trackTitleAt(index) {
                const item = itemAtIndex(index)
                return item ? item.modelData.title : ""
            }
            onContentYChanged: {
                contentScroll.maybeLoadVirtual(contentY, height, contentHeight)
                if (!app.restoringScroll && app.activeScrollKey === app.scrollKey) {
                    const old = app.scrollPositions[app.activeScrollKey] || { outer: 0 }
                    app.scrollPositions[app.activeScrollKey] = { outer: old.outer, inner: contentY }
                }
            }
            onContentHeightChanged: contentScroll.maybeLoadVirtual(contentY, height, contentHeight)
            HoverHandler { onHoveredChanged: contentScroll.virtualCollectionHovered = hovered }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Item {
                id: tile
                required property var modelData
                required property int index
                readonly property bool playlist: (modelData.urn || "").indexOf("soundcloud:playlists:") === 0
                width: virtualCoverGrid.tileWidth
                height: virtualCoverGrid.tileHeight
                function playTrack() { if (playlist) playlistView.begin(modelData); else sc.play(modelData) }
                function enqueueTrack() { sc.enqueue(modelData) }
                Component.onCompleted: virtualCoverGrid.activeDelegateCount++
                Component.onDestruction: virtualCoverGrid.activeDelegateCount--
                HoverHandler { id: tileHover }

                Rectangle {
                    anchors.fill: parent; radius: 16; color: app.panel
                    border.color: app.dark ? "#353535" : "#e3e0e3"
                }
                Rectangle {
                    id: artwork; x: 8; y: 8; width: parent.width - 16; height: width; radius: app.artworkRadius; color: (app.dark ? "#2b2b2b" : "#f0f0f0"); border.color: (app.dark ? "#3b3b3b" : "#eaeaea"); clip: true
                    Glyph { anchors.centerIn: parent; width: 40; height: 40; name: tile.playlist ? "grid" : "music"; ink: (app.dark ? "#777777" : "#bcbcbc") }
                    Artwork { radius: app.artworkRadius; anchors.fill: parent; source: app.cover(tile.modelData); }
                    TapHandler { onTapped: app.openTrack(tile.modelData) }
                    Rectangle { anchors.fill: parent; radius: parent.radius; color: "transparent"; border.color: "#14000000" }
                    Rectangle {
                        visible: !tile.playlist
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                        width: 30; height: 30; radius: 15; color: app.dark ? "#262626" : "white"
                        LikeButton { anchors.fill: parent; round: true; track: tile.modelData }
                    }
                    Rectangle {
                        opacity: tileHover.hovered || Qt.platform.os === "android" ? 1 : 0
                        visible: opacity > 0; enabled: opacity > 0.5
                        Behavior on opacity { NumberAnimation { duration: app.motion * 0.65; easing.type: Easing.OutCubic } }
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 9
                        width: 32; height: 32; radius: 16; color: app.dark ? "#f5262626" : "#f5ffffff"
                        Tool { anchors.fill: parent; round: true; symbol: tile.playlist ? "arrow" : "play"; text: tile.playlist ? "Открыть плейлист" : "Слушать"; onClicked: tile.playTrack() }
                    }
                    Rectangle {
                        opacity: tileHover.hovered && !tile.playlist ? 1 : 0
                        visible: opacity > 0; enabled: opacity > 0.5
                        Behavior on opacity { NumberAnimation { duration: app.motion * 0.65; easing.type: Easing.OutCubic } }
                        x: 9; y: 9; width: 27; height: 27; radius: 14; color: app.dark ? "#f5262626" : "#f5ffffff"
                        Tool { anchors.fill: parent; round: true; symbol: "menu"; text: "Действия с треком"; onClicked: app.openTrackActions(tile.modelData) }
                    }
                }
                Txt { x: 8; y: artwork.y + artwork.height + 8; width: parent.width - 16; text: tile.modelData.title || "Без названия"; font.pixelSize: 13; TapHandler { onTapped: app.openTrack(tile.modelData) } }
                Txt { x: 8; y: artwork.y + artwork.height + 25; width: parent.width - 16; text: tile.modelData.user?.username || (tile.playlist ? (tile.modelData.track_count || 0) + " треков" : "SoundCloud"); color: app.muted; font.pixelSize: 11; TapHandler { enabled: !!tile.modelData.user?.urn; onTapped: app.openProfile(tile.modelData.user) } }
            }
        }
    }
    Component {
        id: trackList
        ListView {
            id: virtualTrackList
            objectName: "trackListView"
            width: parent.width
            height: Math.min(count * 58, Math.max(58, contentScroll.height - y - 110))
            clip: true
            spacing: 0
            Rectangle {
                parent: virtualTrackList
                anchors.fill: parent
                z: -1
                radius: 22
                visible: virtualTrackList.count > 0
                color: app.panel

            }
            cacheBuffer: Math.max(58 * 4, height / 2)
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            model: app.collectionQuery.trim() ? app.filteredCollection : sc.trackModel
            property int activeDelegateCount: 0
            function lastTrackTitle() {
                const item = count > 0 ? itemAtIndex(count - 1) : null
                return item ? item.modelData.title : ""
            }
            function maybeLoad() {
                contentScroll.maybeLoadVirtual(contentY, height, contentHeight)
            }
            onContentYChanged: {
                maybeLoad()
                if (!app.restoringScroll && app.activeScrollKey === app.scrollKey) {
                    const old = app.scrollPositions[app.activeScrollKey] || { outer: 0 }
                    app.scrollPositions[app.activeScrollKey] = { outer: old.outer, inner: contentY }
                }
            }
            onContentHeightChanged: maybeLoad()
            HoverHandler { onHoveredChanged: contentScroll.virtualCollectionHovered = hovered }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool currentTrack: !!modelData.urn && app.api.current.urn === modelData.urn
                width: virtualTrackList.width; height: 58; radius: 12
                Component.onCompleted: virtualTrackList.activeDelegateCount++
                Component.onDestruction: virtualTrackList.activeDelegateCount--
                color: "transparent"
                Rectangle {
                    anchors.fill: parent; anchors.topMargin: 3; anchors.bottomMargin: 3
                    radius: 12
                    color: row.currentTrack ? Qt.alpha(app.accent, app.dark ? 0.20 : 0.13)
                         : rowHover.hovered ? Qt.alpha(app.accent, 0.055)
                         : "transparent"
                    border.width: 0
                    Behavior on color { ColorAnimation { duration: app.motion / 2 } }
                    Rectangle {
                        x: 0; anchors.verticalCenter: parent.verticalCenter
                        width: 3; height: 22; radius: 1.5
                        visible: row.currentTrack; color: app.accent
                    }
                }
                HoverHandler { id: rowHover }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; anchors.topMargin: 7; anchors.bottomMargin: 7; spacing: 10
                    Tool {
                        symbol: row.currentTrack && app.api.playing ? "pause" : "play"
                        text: row.currentTrack && app.api.playing ? "Пауза" : "Слушать"
                        active: row.currentTrack
                        round: true
                        onClicked: { if (row.currentTrack) sc.togglePlayback(); else if ((row.modelData.urn || "").startsWith("soundcloud:playlists:")) playlistView.begin(row.modelData); else sc.play(row.modelData) }
                        implicitWidth: 28
                    }
                    Artwork { radius: app.artworkRadius; width: 40; height: 40; source: app.cover(row.modelData); TapHandler { onTapped: app.openTrack(row.modelData) } }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Txt { Layout.fillWidth: true; text: row.modelData.title || "Без названия"; font.pixelSize: 12; font.weight: row.currentTrack ? Font.DemiBold : Font.Medium; TapHandler { onTapped: app.openTrack(row.modelData) } }
                        Txt { Layout.fillWidth: true; text: row.modelData.user?.username || "SoundCloud"; color: app.muted; font.pixelSize: 11; TapHandler { enabled: !!row.modelData.user?.urn; onTapped: app.openProfile(row.modelData.user) } }
                    }
                    LikeButton { visible: (row.modelData.urn || "").startsWith("soundcloud:tracks:"); track: row.modelData }
                    Tool { visible: (row.modelData.urn || "").indexOf("soundcloud:tracks:") === 0; symbol: "menu"; text: "Действия с треком"; onClicked: app.openTrackActions(row.modelData) }
                    Txt { text: app.time(row.modelData.duration); color: app.muted; font.pixelSize: 11; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignRight }
                }
            }
        }
    }
    component SettingSlider: RowLayout {
        property string label
        property string suffix
        property real minimum: 0
        property real maximum: 24
        property real value: 0
        signal edited(real value)
        width: Math.min(parent.width, 480); spacing: 16
        Txt { text: parent.label; Layout.preferredWidth: 110 }
        LineSlider { Layout.fillWidth: true; from: parent.minimum; to: parent.maximum; stepSize: 1; value: parent.value; onMoved: parent.edited(value) }
        Txt { text: Math.round(parent.value) + parent.suffix; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight; color: app.muted }
    }
    component FormBlock: CapsuleSection {
        id: form
        property string heading
        Txt { width: parent.width; text: form.heading; font.pixelSize: 18; font.weight: Font.DemiBold; bottomPadding: 6 }
    }
    PluginToolsPopup { dark: app.dark; ink: app.ink; muted: app.muted; id: pluginTools; manager: app.extensions }
    Connections {
        target: app.extensions
        function onNavigationRequested(destination) {
            switch (destination) {
            case "profile": app.openProfile(null); break
            case "playlists": app.section = 2; break
            case "search": app.section = 0; searchField.forceActiveFocus(); break
            case "likes": case "history": case "feed": app.selectLibrary(destination); break
            }
            pluginTools.close()
        }
    }
    Component {
        id: pluginsPage
        Column {
            spacing: 18
            Tool { text: "‹ Настройки"; onClicked: app.settingGroup = 0 }
            PluginManagerView { dark: app.dark; ink: app.ink; muted: app.muted; width: parent.width; manager: app.extensions; appearanceHint: app.chosenTheme || prefs.coverRadiusOverride ? "Пользовательское оформление перекрывает соответствующие свойства темы плагина." : "" }
        }
    }
    Component {
        id: settingsPage
        Column {
            spacing: 16
            FormBlock {
                width: parent.width; heading: "Тема оформления"
                CheckBox { InteractionFeedback {} text: "Тёмная тема"; checked: prefs.darkMode; onToggled: app.switchTheme(checked) }
                ComboBox { InteractionFeedback {}
                    width: parent.width; model: ["Стандартная"].concat(app.themes.map(t => t.name))
                    currentIndex: Math.max(0, app.themes.findIndex(t => t.name === prefs.themeName) + 1)
                    onActivated: index => prefs.themeName = index ? app.themes[index - 1].name : ""
                }
                Flow {
                    width: parent.width; spacing: 8
                    Tool { text: "Создать тему"; onClicked: themeEditor.begin(false) }
                    Tool { visible: !!app.chosenTheme; text: "Изменить"; onClicked: themeEditor.begin(true) }
                    Tool { visible: !!app.chosenTheme; text: "Удалить"; onClicked: {
                        const rest = app.themes.filter(t => t.name !== prefs.themeName)
                        prefs.themeName = ""; prefs.savedThemes = JSON.stringify(rest)
                    } }
                }
            }
            Tool { text: "Плагины…"; onClicked: { app.pluginMessage = ""; app.settingGroup = 1 } }
            FormBlock {
                width: parent.width; heading: "Интерфейс"
                Row { spacing: 12; Txt { text: "Вид медиатеки"; anchors.verticalCenter: parent.verticalCenter } Tool { text: "Обложки"; active: prefs.gridView; onClicked: prefs.gridView = true } Tool { text: "Список"; active: !prefs.gridView; onClicked: prefs.gridView = false } }
                Row { spacing: 12; Txt { text: "Громкость"; anchors.verticalCenter: parent.verticalCenter } VolumeSlider { width: 180 } }
                CheckBox { InteractionFeedback {} text: "Плавный переход между треками (3 секунды)"; checked: !!app.api.crossfade; onToggled: sc.setCrossfade(checked) }
            }
            FormBlock {
                width: parent.width; heading: "Форма и движение"
                Row { spacing: 16
                    Artwork { radius: app.artworkRadius; width: 64; height: 64; source: app.cover(app.api.current) }
                    Avatar { width: 64; height: 64; source: app.imageUrl(app.api.profile.avatar_url || "") }
                }
                SettingSlider { label: "Обложки"; suffix: " px"; maximum: 24; value: prefs.coverRadius; onEdited: v => { prefs.coverRadius = v; prefs.coverRadiusOverride = true } }
                SettingSlider { label: "Аватары"; suffix: " %"; maximum: 50; value: prefs.avatarRoundness; onEdited: v => prefs.avatarRoundness = v }
                SettingSlider { label: "Панели"; suffix: " px"; maximum: 24; value: prefs.panelRadius; onEdited: v => prefs.panelRadius = v }
                CheckBox { InteractionFeedback {} text: "Анимации и плавная прокрутка"; checked: prefs.motion; onToggled: prefs.motion = checked }
                SettingSlider { label: "Длительность"; suffix: " мс"; minimum: 120; maximum: 500; value: prefs.animationMs; enabled: prefs.motion; onEdited: v => prefs.animationMs = v }
                CheckBox { InteractionFeedback {} text: "Автоматически загружать ещё при прокрутке"; checked: prefs.autoLoad; onToggled: prefs.autoLoad = checked }
                Tool { text: "Сбросить оформление"; onClicked: { prefs.coverRadius = 8; prefs.coverRadiusOverride = false; prefs.avatarRoundness = 50; prefs.panelRadius = 12; prefs.animationMs = 240; prefs.motion = true; prefs.autoLoad = true } }
            }
            FormBlock {
                width: parent.width; heading: "Discord Rich Presence"
                visible: typeof discordPresence !== "undefined" && discordPresence.available
                CheckBox { InteractionFeedback {}
                    objectName: "discordPresenceToggle"
                    text: "Показывать музыку в Discord"
                    checked: typeof discordPresence !== "undefined" && discordPresence.enabled
                    onToggled: discordPresence.enabled = checked
                }
                Txt { width: parent.width; text: "Трек, исполнитель, обложка и время прослушивания в вашем профиле Discord. Кнопки: открыть трек и скачать SCrip."; color: app.muted; wrapMode: Text.WordWrap }
                Txt { width: parent.width; text: typeof discordPresence !== "undefined" ? discordPresence.status : ""; color: app.muted; wrapMode: Text.WordWrap }
                Txt { text: "Свой Application ID Discord (необязательно)"; color: app.muted }
                Field {
                    objectName: "discordApplicationId"
                    width: parent.width
                    placeholderText: "Пустое поле — стандартный ID приложения"
                    text: typeof discordPresence !== "undefined" ? discordPresence.applicationId : ""
                    maximumLength: 20
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,20}/ }
                    onEditingFinished: discordPresence.applicationId = text
                    Accessible.name: "Application ID Discord"
                }
                Txt { width: parent.width; text: "Стандартный ID уже настроен. Очистите поле, чтобы восстановить его."; color: app.muted; wrapMode: Text.WordWrap }
                Tool { text: "Discord Developer Portal ↗"; onClicked: sc.openExternal("https://discord.com/developers/applications") }
            }
            FormBlock {
                width: parent.width; heading: "Аккаунт"
                Txt { text: app.api.profile.username || "SoundCloud"; font.pixelSize: 14 }
                Tool { text: "Открыть профиль SoundCloud"; onClicked: sc.openExternal(app.api.profile.permalink_url || "https://soundcloud.com") }
                Tool { text: "Выйти из аккаунта"; onClicked: disconnectDialog.open() }
            }
            FormBlock {
                width: parent.width; heading: "SCrip"
                Txt { text: "Версия " + Qt.application.version; color: app.muted }
                Txt { text: app.api.updateError || app.api.updateResult || (app.api.updateAvailable ? "Доступна версия " + app.api.latestVersion : "Проверка ещё не выполнялась"); color: app.api.updateError ? "#a22c39" : app.api.updateAvailable ? app.accent : app.muted; width: parent.width; wrapMode: Text.WordWrap }
                Txt { visible: !!app.api.updateDownloading; text: app.api.updateTotal > 0 ? (Number(app.api.updateDownloaded / 1048576).toFixed(1) + " МБ / " + Number(app.api.updateTotal / 1048576).toFixed(1) + " МБ · " + Math.floor(100 * app.api.updateDownloaded / app.api.updateTotal) + "%") : (app.api.updateDownloaded + " байт скачано"); color: app.muted }
                ProgressBar { visible: !!app.api.updateDownloading; width: parent.width; indeterminate: !(app.api.updateTotal > 0); from: 0; to: 100; value: app.api.updateTotal > 0 ? 100 * app.api.updateDownloaded / app.api.updateTotal : 0 }
                Tool { text: "Проверить обновления"; visible: !app.api.updateChecking && !app.api.updateDownloading && !app.api.updateInstalling && !app.api.updateReady; enabled: !app.api.updateChecking; onClicked: sc.checkForUpdates(true) }
                Tool { text: "Скачать обновление"; visible: !!app.api.updateAvailable && !app.api.updateDownloading && !app.api.updateReady && !app.api.updateInstalling; onClicked: sc.downloadUpdate() }
                Tool { text: "Отмена"; visible: !!app.api.updateDownloading; onClicked: sc.cancelUpdateDownload() }
                Tool { text: "Установить сейчас"; visible: !!app.api.updateReady && !app.api.updateInstalling; onClicked: sc.installUpdate() }
                Txt { text: "Независимый клиент SoundCloud"; color: app.muted }
            }
        }
    }
    component BrandMark: ControlsImpl.IconImage {
        source: "assets/blob.png"
        color: app.accent
        fillMode: Image.PreserveAspectFit
        mipmap: true
    }
    component Sheet: Popup {
        id: sheet
        opacity: 0; scale: 0.985
        enter: SurfaceTransition { duration: app.motion }
        exit: SurfaceTransition { duration: app.motion; leaving: true }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true; focus: true; padding: 24
        width: Math.min(460, app.width - 60)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: (app.dark ? app.panel : "#fafafa"); radius: 22;  }
        Overlay.modal: Rectangle { color: "#28000000" }
    }
    component TrackAction: AbstractButton {
        id: action
        property string symbol: "music"
        Layout.fillWidth: true
        implicitHeight: 42
        leftPadding: 12; rightPadding: 12
        hoverEnabled: true
        Accessible.name: text
        opacity: enabled ? 1 : 0.4
        background: Rectangle { radius: 10; color: action.down || action.hovered ? app.raised : "transparent" }
        contentItem: RowLayout {
            spacing: 14
            Glyph { name: action.symbol; ink: app.muted; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
            Txt { text: action.text; Layout.fillWidth: true; font.pixelSize: 13 }
        }
    }
    Sheet {
        id: trackActions
        objectName: "desktopTrackMenu"
        property var track: ({})
        width: Math.min(440, app.width - 48)
        height: Math.min(parent.height - 64, actionContent.implicitHeight + topPadding + bottomPadding)
        contentItem: Flickable {
            clip: true
            contentHeight: actionContent.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: actionContent
                width: parent.width
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true; Layout.bottomMargin: 14; spacing: 12
                    Artwork { source: app.cover(trackActions.track); radius: app.artworkRadius; Layout.preferredWidth: 48; Layout.preferredHeight: 48 }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Txt { text: trackActions.track.title || "Без названия"; Layout.fillWidth: true; font.weight: Font.DemiBold }
                        Txt { text: trackActions.track.user?.username || "SoundCloud"; Layout.fillWidth: true; color: app.muted; font.pixelSize: 12 }
                    }
                    Tool { symbol: "close"; text: "Закрыть меню"; onClicked: trackActions.close() }
                }
                Rectangle { Layout.fillWidth: true; Layout.bottomMargin: 8; height: 1; color: app.line }
                TrackAction { symbol: "heart"; text: trackActions.track.user_favorite ? "Убрать из любимых" : "Добавить в любимые"; onClicked: { sc.like(trackActions.track); trackActions.close() } }
                TrackAction { symbol: "plus"; text: "Добавить в очередь"; onClicked: { sc.enqueue(trackActions.track); trackActions.close() } }
                TrackAction { symbol: "music"; text: "Сохранить в плейлист"; onClicked: { trackActions.close(); albumPicker.begin(trackActions.track) } }
                TrackAction { symbol: "profile"; text: "Профиль исполнителя"; enabled: !!trackActions.track.user?.urn; onClicked: { trackActions.close(); playlistView.close(); app.openProfile(trackActions.track.user) } }
                TrackAction { symbol: "play"; text: "Станция по треку"; onClicked: { trackActions.close(); playlistView.beginStation(trackActions.track, false) } }
                TrackAction { symbol: "cloud"; text: "Открыть в SoundCloud"; enabled: !!trackActions.track.permalink_url; onClicked: { sc.openExternal(trackActions.track.permalink_url); trackActions.close() } }
                TrackAction { symbol: "list"; text: "Копировать название и исполнителя"; onClicked: { sc.copyText((trackActions.track.title || "Без названия") + " — " + (trackActions.track.user?.username || "SoundCloud")); trackActions.close() } }
                TrackAction { symbol: "arrow"; text: "Копировать ссылку"; enabled: !!trackActions.track.permalink_url; onClicked: { sc.copyText(trackActions.track.permalink_url); trackActions.close() } }
            }
        }
    }
    Sheet {
        id: albumPicker
        property var track: ({})
        function begin(item) { track = item; albumTitle.text = ""; sc.clearError(); open(); sc.listDestinations() }
        Connections { target: sc; function onTrackSaved() { albumPicker.close() } }
        contentItem: ColumnLayout {
            spacing: 10
            Txt { text: "Добавить в альбом"; font.pixelSize: 20; font.weight: Font.DemiBold }
            Txt { Layout.fillWidth: true; text: albumPicker.track.title || ""; color: app.muted }
            ListView {
                id: albums
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(count * 36, 216); clip: true
                model: app.api.destinations || []
                delegate: Tool { required property var modelData; width: albums.width; height: 36; text: modelData.title; enabled: !app.api.savingTrack; onClicked: sc.saveToPlaylist(albumPicker.track.urn, modelData.urn, "") }
            }
            Tool { visible: !!app.api.destinationNext; enabled: !app.api.loadingDestinations; text: "Ещё плейлисты"; onClicked: sc.listDestinations(app.api.destinationNext) }
            Field { id: albumTitle; Layout.fillWidth: true; placeholderText: "Название нового альбома" }
            Tool { text: "Создать и добавить"; enabled: !!albumTitle.text.trim() && !app.api.savingTrack; onClicked: sc.saveToPlaylist(albumPicker.track.urn, "", albumTitle.text) }
            Txt { visible: !!app.api.error; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: app.api.error || ""; color: app.muted }
        }
    }
    Sheet {
        id: themeEditor
        property color draft: app.baseAccent
        property string original: ""
        function begin(edit) {
            original = edit ? prefs.themeName : ""
            themeTitle.text = original
            draft = app.baseAccent
            tintSlider.value = edit ? app.chosenTheme.tint : 5
            open()
            themeTitle.forceActiveFocus()
        }
        contentItem: Column {
            spacing: 14
            Txt { text: themeEditor.original ? "Изменить тему" : "Новая тема"; font.pixelSize: 20; font.weight: Font.DemiBold }
            Field { id: themeTitle; width: parent.width; maximumLength: 40; placeholderText: "Название темы"; Accessible.name: placeholderText }
            Rectangle {
                width: parent.width; height: 100; radius: 12
                color: Qt.tint((app.dark ? "#1a1a1a" : "white"), Qt.alpha(themeEditor.draft, app.dark ? tintSlider.value / 200 : tintSlider.value / 100))
                border.color: "#e6e3e5"
                BrandMark { anchors.centerIn: parent; width: 126; height: width; color: themeEditor.draft }
            }
            Tool { text: "Выбрать цвет…"; onClicked: { themeColor.selectedColor = themeEditor.draft; themeColor.open() } }
            Txt { text: "Оттенок фона · " + Math.round(tintSlider.value) + " %" }
            LineSlider { id: tintSlider; width: parent.width; from: 0; to: 20; stepSize: 1; Accessible.name: "Оттенок фона" }
            Row {
                spacing: 8
                Tool {
                    text: "Сохранить"; primary: true
                    readonly property string titleValue: themeTitle.text.trim()
                    enabled: titleValue.length > 0 && titleValue !== "Стандартная" && !app.themes.some(t => t.name === titleValue && t.name !== themeEditor.original)
                    onClicked: {
                        const entry = {name: titleValue, accent: themeEditor.draft.toString(), tint: Math.round(tintSlider.value)}
                        const next = themeEditor.original ? app.themes.map(t => t.name === themeEditor.original ? entry : t) : app.themes.concat([entry])
                        prefs.savedThemes = JSON.stringify(next); prefs.themeName = entry.name; themeEditor.close()
                    }
                }
                Tool { text: "Отмена"; onClicked: themeEditor.close() }
            }
        }
        Dialogs.ColorDialog { id: themeColor; title: "Цвет темы"; onAccepted: themeEditor.draft = selectedColor }
    }
    Sheet {
        id: playlistDialog
        contentItem: Column {
            spacing: 16
            Txt { text: "Новый плейлист"; font.pixelSize: 20; font.weight: Font.DemiBold }
            Field { id: playlistTitle; width: parent.width; placeholderText: "Название плейлиста" }
            CheckBox { InteractionFeedback {} id: includeCurrent; text: "Добавить текущий трек"; enabled: !!app.api.current.urn }
            Row { spacing: 8; Tool { text: "Создать"; primary: true; enabled: playlistTitle.text.trim().length > 0; onClicked: { sc.createPlaylist(playlistTitle.text, includeCurrent.checked); playlistDialog.close(); playlistTitle.clear() } } Tool { text: "Отмена"; onClicked: playlistDialog.close() } }
            Txt { text: "Плейлист будет приватным."; color: app.muted; font.pixelSize: 11 }
        }
    }
    Sheet {
        id: disconnectDialog
        contentItem: Column {
            spacing: 16
            Txt { text: "Выйти из SoundCloud?"; font.pixelSize: 19; font.weight: Font.DemiBold }
            Txt { width: parent.width; wrapMode: Text.WordWrap; elide: Text.ElideNone; text: "Доступ SCrip к аккаунту будет отозван. Ваша музыка и плейлисты останутся в SoundCloud."; color: app.muted }
            Row { spacing: 8; Tool { text: "Выйти"; primary: true; onClicked: { sc.logout(); disconnectDialog.close() } } Tool { text: "Отмена"; onClicked: disconnectDialog.close() } }
        }
    }
    Component {
        id: profileHeader
        Column {
            spacing: 24
            Rectangle {
                width: parent.width; height: 196; radius: 22; color: (app.dark ? "#242424" : "#f3f3f5");
                readonly property bool creator: app.displayedProfile.urn === "soundcloud:users:1675018934"
                border.width: creator ? 2 : 0; border.color: app.accent
                Rectangle {
                    visible: parent.creator; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16
                    width: creatorLabel.implicitWidth + 22; height: 26; radius: 13; color: app.accent
                    SequentialAnimation on opacity {
                        running: parent.visible && prefs.motion; loops: Animation.Infinite
                        NumberAnimation { to: 0.7; duration: 1600 }
                        NumberAnimation { to: 1; duration: 1600 }
                    }
                    Txt { id: creatorLabel; anchors.centerIn: parent; text: "✦ Создатель SCrip"; color: app.foreground(app.accent); font.weight: Font.DemiBold }
                }
                Avatar { x: 24; y: 32; width: 96; height: 96; source: app.imageUrl(app.displayedProfile.avatar_url || "") }
                Column {
                    x: 144; y: 32; width: parent.width - 168; spacing: 8
                    Txt { width: parent.width; text: app.displayedProfile.username || "Профиль"; font.pixelSize: 28; font.weight: Font.DemiBold; font.letterSpacing: -0.6 }
                    Txt {
                        width: parent.width; color: app.accent; font.weight: Font.DemiBold
                        text: {
                            const p = app.displayedProfile
                            const products = (p.subscriptions || []).map(s => (s.product?.id || "") + " " + (s.product?.name || "")).join(" ")
                            const info = (products + " " + (p.plan || "")).toLowerCase()
                            const badges = []
                            if (/artist[ -]pro|pro[ -]unlimited|pro plus/.test(info)) badges.push("Artist Pro")
                            if (/go[ +_-]plus|go\+/.test(info)) badges.push("Go+")
                            else if (/\bgo\b/.test(info)) badges.push("Go")
                            return badges.join("  ·  ")
                        }
                        visible: !!text
                    }
                    Txt { width: parent.width; visible: !!app.displayedProfile.full_name; text: app.displayedProfile.full_name || ""; color: app.muted }
                    Txt { width: parent.width; visible: !!(app.displayedProfile.city || app.displayedProfile.country); text: [app.displayedProfile.city, app.displayedProfile.country].filter(Boolean).join(", "); color: app.muted; font.pixelSize: 12 }
                }
                Row {
                    x: 24; y: 148; spacing: 28
                    Repeater {
                        model: [{value:app.displayedProfile.followers_count || 0, caption:"подписчиков"}, {value:app.displayedProfile.followings_count || 0, caption:"подписок"}, {value:app.displayedProfile.track_count || 0, caption:"треков"}]
                        Column { required property var modelData; spacing: 3; Txt { text: modelData.value; font.pixelSize: 17; font.weight: Font.DemiBold } Txt { text: modelData.caption; font.pixelSize: 11; color: app.muted } }
                    }
                }
                Tool { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 20; symbol: "arrow"; text: "Открыть на SoundCloud"; onClicked: sc.openExternal(app.displayedProfile.permalink_url || "https://soundcloud.com") }
            }
            CapsuleSection {
                width: parent.width; visible: !!app.displayedProfile.description
                contentItem: Txt { text: app.displayedProfile.description || ""; wrapMode: Text.WordWrap; maximumLineCount: 5; lineHeight: 1.5; color: app.muted }
            }
            Row {
                width: parent.width; spacing: 24
                Repeater {
                    model: app.guest ? ["Треки", "Плейлисты"] : ["Треки", "Плейлисты", "Избранное", "Репосты"]
                    AbstractButton {
                        InteractionFeedback {}
                        id: profileLink
                        required property string modelData
                        required property int index
                        width: profileTabLabel.implicitWidth; height: 36
                        onClicked: app.profileTab = index
                        contentItem: Txt { id: profileTabLabel; text: profileLink.modelData; color: app.profileTab === profileLink.index ? app.accent : app.muted; font.weight: Font.Medium }
                        background: Item { Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; radius: 1; color: app.accent; opacity: app.profileTab === profileLink.index ? 1 : 0; Behavior on opacity { NumberAnimation { duration: app.motion } } } }
                    }
                }
            }
        }
    }
    component Avatar: Artwork { radius: Math.min(width, height) * prefs.avatarRoundness / 100 }
    component Inspector: ColumnLayout {
        id: inspector
        signal dismiss()
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Txt { text: "Сейчас играет"; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Tool { symbol: "close"; text: "Скрыть панель"; onClicked: inspector.dismiss() }
        }
        Rectangle {
            visible: !!app.api.current.urn && inspector.height >= 480
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(width, inspector.height * 0.34); radius: app.artworkRadius; color: (app.dark ? "#2b2b2b" : "#f0eef0"); clip: true
            Glyph { anchors.centerIn: parent; width: 36; height: 36; name: "music"; ink: (app.dark ? "#777777" : "#bbb4bb") }
            Artwork { radius: app.artworkRadius; anchors.fill: parent; source: app.cover(app.api.current); TapHandler { onTapped: app.openTrack(app.api.current, true) } }
        }
        RowLayout {
            visible: !!app.api.current.urn; Layout.fillWidth: true; spacing: 8
            ColumnLayout {
                Layout.fillWidth: true; spacing: 6
                Txt { Layout.fillWidth: true; text: app.api.current.title || ""; wrapMode: Text.WordWrap; maximumLineCount: 2; font.pixelSize: 18; font.weight: Font.DemiBold; lineHeight: 1.2; TapHandler { onTapped: app.openTrack(app.api.current, true) } }
                Txt { Layout.fillWidth: true; text: app.api.current.user?.username || "SoundCloud"; color: app.muted; font.pixelSize: 12; TapHandler { enabled: !!app.api.current.user?.urn; onTapped: app.openProfile(app.api.current.user) } }
            }
            LikeButton { track: app.api.current }
        }
        Txt { visible: !!app.api.current.genre; Layout.fillWidth: true; text: app.api.current.genre || ""; color: app.muted; font.pixelSize: 11 }
        Column {
            visible: !app.api.current.urn; Layout.fillWidth: true; spacing: 12; topPadding: 32; bottomPadding: 32
            Glyph { anchors.horizontalCenter: parent.horizontalCenter; name: "music"; width: 36; height: 36; ink: (app.dark ? "#777777" : "#b5afb5") }
            Txt { width: parent.width; text: "Выберите трек"; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 14; font.weight: Font.Medium }
            Txt { width: parent.width; text: "Включите трек в медиатеке.
Здесь появятся обложка и очередь."; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; color: app.muted; font.pixelSize: 12; lineHeight: 1.4 }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: (app.dark ? "#323232" : "#eaeaec") }
        RowLayout { Layout.fillWidth: true; Txt { text: "Очередь"; font.weight: Font.DemiBold; Layout.fillWidth: true } Txt { text: app.api.queue.length; color: app.muted; font.pixelSize: 11 } }
        Txt { visible: app.api.queue.length === 0; text: "Добавляйте треки кнопкой +"; color: app.muted; font.pixelSize: 12 }
        ListView {
            id: queueList
            objectName: "queueListView"
            property int dragFrom: -1
            property int dragTo: -1
            property bool removing: false
            property real dragY: 0
            property var dragTrack: ({})
            onDragFromChanged: dragTo = dragFrom
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 64
            clip: true; model: app.api.queue; spacing: 4
            currentIndex: app.api.queueIndex || 0
            onCurrentIndexChanged: { if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain) }
            delegate: Rectangle {
                id: queuedTrack
                required property var modelData
                required property int index
                width: Math.max(0, queueList.width - queueBar.implicitWidth - 4); height: 52; radius: 6
                color: index === app.api.queueIndex ? app.raised : queueHover.hovered ? app.panel : "transparent"

                opacity: queueList.dragFrom === index ? 0.35 : 1
                Behavior on color { ColorAnimation { duration: app.motion * 0.6 } }
                HoverHandler { id: queueHover }
                Rectangle {
                    visible: queueList.dragFrom >= 0 && queueList.dragTo === queuedTrack.index
                    anchors.left: parent.left; anchors.right: parent.right
                    y: queueList.dragTo > queueList.dragFrom ? parent.height - height : 0
                    height: 2; color: app.accent
                }
                MouseArea {
                    anchors.fill: parent; acceptedButtons: Qt.RightButton
                    enabled: !queueList.removing && queueList.dragFrom < 0
                    onClicked: { queueList.removing = true; removeAnimation.start() }
                }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 6; spacing: 8
                    Item {
                        width: 34; height: 34
                        Artwork { radius: app.artworkRadius; anchors.fill: parent; source: app.cover(queuedTrack.modelData); TapHandler { onTapped: app.openTrack(queuedTrack.modelData) } }
                        Tool { anchors.fill: parent; symbol: queuedTrack.index === app.api.queueIndex && app.api.playing ? "pause" : "play"; text: "Слушать"; onClicked: { if (queuedTrack.index === app.api.queueIndex) sc.togglePlayback(); else sc.playQueue(queuedTrack.index) } }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Txt { Layout.fillWidth: true; text: queuedTrack.modelData.title || "Трек"; font.pixelSize: 12; TapHandler { onTapped: app.openTrack(queuedTrack.modelData) } }
                        Txt { Layout.fillWidth: true; text: queuedTrack.modelData.user?.username || ""; color: app.muted; font.pixelSize: 10 }
                    }
                    Item {
                        id: queueActions
                        objectName: "queueHandle" + queuedTrack.index
                        Layout.preferredWidth: 24; Layout.fillHeight: true
                        property point pointerPosition
                        function updateTarget() {
                            const pos = mapToItem(queueList.contentItem, pointerPosition.x, pointerPosition.y)
                            queueList.dragTo = Math.max(0, Math.min(queueList.count - 1, Math.floor(pos.y / 56)))
                            const viewportPos = mapToItem(queueList, pointerPosition.x, pointerPosition.y)
                            queueList.dragY = Math.max(0, Math.min(queueList.height - 52, viewportPos.y - 26))
                        }
                        Glyph { anchors.centerIn: parent; width: 16; height: 16; name: "list"; ink: app.ink }
                        MouseArea {
                            id: queueDrag
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            enabled: !queueList.removing
                            preventStealing: true
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            onPressed: function(mouse) {
                                queueActions.pointerPosition = Qt.point(mouse.x, mouse.y)
                                queueList.dragTrack = queuedTrack.modelData
                                const pos = queueActions.mapToItem(queueList, mouse.x, mouse.y)
                                queueList.dragY = Math.max(0, Math.min(queueList.height - 52, pos.y - 26))
                                queueList.dragFrom = queuedTrack.index
                                queueActions.updateTarget()
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed) return
                                queueActions.pointerPosition = Qt.point(mouse.x, mouse.y)
                                queueActions.updateTarget()
                            }
                            onReleased: {
                                const from = queueList.dragFrom, to = queueList.dragTo
                                queueList.dragFrom = -1
                                if (from >= 0 && to >= 0) sc.moveQueue(from, to)
                            }
                            onCanceled: queueList.dragFrom = -1
                        }
                        Timer {
                            interval: 30; repeat: true; running: queueDrag.pressed
                            onTriggered: {
                                const pos = queueActions.mapToItem(queueList, queueActions.pointerPosition.x, queueActions.pointerPosition.y)
                                if (pos.y < 30) queueList.contentY = Math.max(queueList.originY, queueList.contentY - 8)
                                else if (pos.y > queueList.height - 30) queueList.contentY = Math.min(Math.max(queueList.originY, queueList.contentHeight - queueList.height), queueList.contentY + 8)
                                queueActions.updateTarget()
                            }
                        }
                    }
                }
                ParallelAnimation {
                    id: removeAnimation
                    NumberAnimation { target: queuedTrack; property: "opacity"; to: 0; duration: app.motion; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: queuedTrack; property: "height"; to: 0; duration: app.motion; easing.type: Easing.InOutQuad }
                    onFinished: { const index = queuedTrack.index; queueList.removing = false; sc.removeQueue(index) }
                }
            }
            Rectangle {
                parent: queueList
                objectName: "queueDragPreview"
                visible: queueList.dragFrom >= 0
                z: 10; x: 0; y: queueList.dragY
                width: Math.max(0, queueList.width - queueBar.implicitWidth - 4); height: 52; radius: 6
                color: app.raised

                Rectangle {
                    anchors.fill: parent; radius: parent.radius
                    color: Qt.alpha(app.accent, 0.12)
                    border.color: Qt.alpha(app.accent, 0.3)
                }
                border.color: app.accent; border.width: 1
                Behavior on y { enabled: queueList.dragFrom >= 0; NumberAnimation { duration: Math.min(app.motion, 90); easing.type: Easing.OutCubic } }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 6; spacing: 8
                    Artwork { radius: app.artworkRadius; width: 34; height: 34; source: app.cover(queueList.dragTrack) }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Txt { Layout.fillWidth: true; text: queueList.dragTrack.title || "Трек"; font.pixelSize: 12 }
                        Txt { Layout.fillWidth: true; text: queueList.dragTrack.user?.username || ""; color: app.muted; font.pixelSize: 10 }
                    }
                    Glyph { Layout.preferredWidth: 24; Layout.preferredHeight: 16; name: "list"; ink: app.ink }
                }
            }
            ScrollBar.vertical: ScrollBar { id: queueBar }
        }
    }
    Popup {
        id: queuePopup; parent: Overlay.overlay
        x: app.width - width - 28; y: app.height - height - 100
        width: Math.min(340, app.width - 60); height: Math.min(640, app.height - 120)
        padding: 18; focus: true; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: (app.dark ? app.panel : "#fafafa"); radius: 14; border.color: (app.dark ? "#404040" : "#dadada");  }
        contentItem: Inspector { onDismiss: queuePopup.close() }
        opacity: 0; scale: 0.985
        enter: SurfaceTransition { duration: app.motion }
        exit: SurfaceTransition { duration: app.motion; leaving: true }
    }

    // Нативное изменение размера без дополнительной рамки оконного менеджера.
    Repeater {
        model: [Qt.TopEdge, Qt.BottomEdge, Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge|Qt.LeftEdge, Qt.TopEdge|Qt.RightEdge, Qt.BottomEdge|Qt.LeftEdge, Qt.BottomEdge|Qt.RightEdge]
        MouseArea {
            required property int modelData
            readonly property bool horizontal: (modelData & Qt.LeftEdge) || (modelData & Qt.RightEdge)
            readonly property bool vertical: (modelData & Qt.TopEdge) || (modelData & Qt.BottomEdge)
            x: (modelData & Qt.RightEdge) ? app.width - width : 0
            y: (modelData & Qt.BottomEdge) ? app.height - height : 0
            width: horizontal ? 6 : app.width; height: vertical ? 6 : app.height
            enabled: app.visibility === Window.Windowed
            cursorShape: horizontal && vertical ? ((modelData === (Qt.TopEdge|Qt.LeftEdge) || modelData === (Qt.BottomEdge|Qt.RightEdge)) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor) : horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: app.startSystemResize(modelData)
        }
    }
}
