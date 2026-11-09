import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import QtCore
import QtQuick.Dialogs as Dialogs

ApplicationWindow {
    id: app
    visible: true
    width: 412
    height: 860
    minimumWidth: 320
    minimumHeight: 480
    title: "SCrip · Android"
    color: "#faf9fa"
    readonly property var api: sc.catalogState
    readonly property color accent: prefs.accentOverride ? prefs.accent : pluginManager.theme.accent || prefs.accent
    readonly property real coverRadius: prefs.coverRadiusOverride ? prefs.coverRadius : pluginManager.theme.coverRadius ?? prefs.coverRadius
    readonly property color ink: "#242426"
    readonly property color muted: "#858589"
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
            if (Qt.platform.os === "android")
                widgetStyle = androidMedia.widgetStyle()
        }
        property int widgetStyle: 0
        onWidgetStyleChanged: if (Qt.platform.os === "android") androidMedia.setWidgetStyle(widgetStyle)
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
    readonly property var profile: guest ? (api.visitedProfile?.urn === guest.urn ? api.visitedProfile : guest) : api.profile
    function openProfile(user) { guest = user || null; profileTab = 0; page = 4; refresh() }

    property int page: 0
    property string library: "likes"
    property string searchKind: "tracks"
    property var selectedTrack: ({})
    property int selectedQueue: -1
    function cover(item) { return (pluginManager.theme.covers === "artist" ? item?.user?.avatar_url || item?.artwork_url || "" : item?.artwork_url || item?.tracks?.[0]?.artwork_url || item?.user?.avatar_url || "").replace("-large.", "-t500x500.") }
    function time(ms) { let s = Math.floor((ms || 0) / 1000); return Math.floor(s / 60) + ":" + (s % 60).toString().padStart(2, "0") }
    function refresh() {
        if (!sc.signedIn) return
        if (page === 0) sc.loadHome()
        else if (page === 1) sc.browse("feed")
        else if (page === 2) sc.browse(searchKind, search.text)
        else if (page === 3) sc.browse(library)
        else if (page === 4) sc.browse(guest ? (profileTab ? "userSets" : "userTracks") : ["mine", "playlists", "likes", "reposts"][profileTab], guest?.urn || "")
    }
    readonly property int motion: prefs.motion ? 360 : 0
    onPageChanged: { refresh(); pageArrival.restart(); Qt.callLater(function() { if (tracks.visible) tracks.positionViewAtBeginning() }) }
    ParallelAnimation {
        id: pageArrival
        NumberAnimation { target: body; property: "opacity"; from: 0; to: 1; duration: app.motion; easing.type: Easing.OutCubic }
        NumberAnimation { target: pageOffset; property: "y"; from: 14; to: 0; duration: app.motion; easing.type: Easing.OutQuint }
    }
    component FineSlider: Slider {
        id: fine
        implicitHeight: 40
        leftPadding: 0; rightPadding: 0
        background: Rectangle {
            x: fine.leftPadding; y: fine.topPadding + fine.availableHeight / 2 - height / 2
            width: fine.availableWidth; height: 3; radius: 1.5; color: "#ded7dc"
            Rectangle { width: fine.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: app.accent }
        }
        handle: Rectangle {
            x: fine.leftPadding + fine.visualPosition * (fine.availableWidth - width)
            y: fine.topPadding + fine.availableHeight / 2 - height / 2
            width: 12; height: 12; radius: 6; color: app.accent
            scale: fine.pressed ? 1.35 : 1
            Behavior on scale { NumberAnimation { duration: prefs.motion ? 150 : 0; easing.type: Easing.OutCubic } }
        }
    }
    component PlayerButton: AbstractButton {
        id: transport
        property string symbol: ""
        property bool active: false
        property bool main: false
        implicitWidth: main ? 64 : 48
        implicitHeight: main ? 64 : 48
        Accessible.name: text
        scale: down ? 0.96 : 1
        Behavior on scale { NumberAnimation { duration: prefs.motion ? 150 : 0; easing.type: Easing.OutCubic } }
        background: Rectangle {
            radius: width / 2
            color: transport.main ? app.accent : transport.down ? "#e9e0e5" : "transparent"
            Behavior on color { ColorAnimation { duration: prefs.motion ? 150 : 0 } }
        }
        contentItem: Item {
            AndroidGlyph { anchors.centerIn: parent; width: transport.main ? 26 : 22; height: width; name: transport.symbol; ink: transport.main ? "white" : transport.active ? app.accent : app.ink }
            Rectangle { visible: transport.active && !transport.main; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; width: 3; height: 3; radius: 1.5; color: app.accent }
        }
    }
    component Sheet: Popup {
        id: sheet
        property real reveal: 1
        // Смещение содержимого сохраняет привязку самой панели к низу экрана.
        onContentItemChanged: if (contentItem) contentItem.transform = [sheetOffset]
        Translate { id: sheetOffset; y: (1 - sheet.reveal) * 36 }
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: app.motion * 0.7; easing.type: Easing.OutCubic }
                NumberAnimation { property: "reveal"; from: 0; to: 1; duration: app.motion; easing.type: Easing.OutQuint }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: app.motion * 0.55; easing.type: Easing.InCubic }
                NumberAnimation { property: "reveal"; to: 0; duration: app.motion * 0.55; easing.type: Easing.InCubic }
            }
        }
    }
    component Action: Button {
        id: control
        property bool primary: false
        property string symbol: ""
        implicitHeight: 48
        leftPadding: 16
        rightPadding: 16
        font.pixelSize: 14
        font.bold: true
        Accessible.name: text
        scale: down ? 0.96 : 1
        Behavior on scale { NumberAnimation { duration: prefs.motion ? 150 : 0; easing.type: Easing.OutCubic } }
        contentItem: Item {
            implicitWidth: control.symbol ? 16 : label.implicitWidth
            implicitHeight: 22
            AndroidGlyph { anchors.centerIn: parent; visible: !!control.symbol; name: control.symbol; ink: control.primary ? "white" : app.ink; width: 22; height: 22 }
            Text { id: label; visible: !control.symbol; anchors.fill: parent; text: control.text; font: control.font; color: control.primary ? "white" : app.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        }
        background: Rectangle { radius: height / 2; color: control.down ? (control.primary ? Qt.darker(app.accent, 1.12) : "#e7e3e7") : control.primary ? app.accent : "#f0edef"; opacity: control.enabled ? 1 : 0.45; Behavior on color { ColorAnimation { duration: app.motion / 2 } } }
    }
    component Caption: Label { color: app.ink; font.pixelSize: 15; elide: Text.ElideRight }
    component Track: ItemDelegate {
        id: row
        required property var modelData
        property bool queued: false
        property int queueIndex: -1
        width: ListView.view.width
        height: 68
        leftPadding: 20
        rightPadding: 12
        background: Rectangle { radius: 16; color: row.down ? "#eeeaed" : "transparent"; Behavior on color { ColorAnimation { duration: app.motion / 2 } } }
        contentItem: RowLayout {
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 44; Layout.preferredHeight: 44
                radius: app.page === 2 && app.searchKind === "users" ? width / 2 : app.coverRadius; color: "#efe8eb"; clip: true
                RoundedImage { radius: app.coverRadius; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0; anchors.fill: parent; source: app.page === 2 && app.searchKind === "users" ? row.modelData.avatar_url || "" : app.cover(row.modelData); avatar: app.page === 2 && app.searchKind === "users" }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Caption { text: row.modelData.title || row.modelData.username || "Без названия"; Layout.fillWidth: true; font.bold: true; color: row.modelData.urn === app.api.current?.urn ? app.accent : app.ink }
                Caption { text: row.modelData.user?.username || (row.modelData.track_count ? row.modelData.track_count + " треков" : "SoundCloud"); Layout.fillWidth: true; color: app.muted; font.pixelSize: 12 }
            }
            Action { text: "Действия"; symbol: "more"; implicitWidth: 48; Accessible.name: "Действия с треком"; onClicked: { app.selectedTrack = row.modelData; app.selectedQueue = row.queued ? row.queueIndex : -1; trackMenu.open() } }
        }
        onClicked: {
            if (queued) sc.playQueue(queueIndex)
            else if (app.page === 2 && app.searchKind === "users") app.openProfile(modelData)
            else sc.play(modelData)
        }
    }

    header: Item {
        id: topBar
        height: 66 + app.headerTopInset
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
                    Caption { text: "SCrip"; color: app.accent; font.bold: true; font.pixelSize: 12 }
                }
                Caption { text: ["Главная", "Лента", "Поиск", "Медиатека", "Профиль", "Настройки", "Плагины"][app.page]; font.pixelSize: 21; font.bold: true }
            }
            AbstractButton {
                id: accountButton
                width: 48; height: 48
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                Accessible.name: app.page === 4 ? "Настройки" : "Мой профиль"
                scale: down ? 0.94 : 1
                Behavior on scale { NumberAnimation { duration: app.motion / 2 } }
                contentItem: Item {
                    RoundedImage { radius: app.coverRadius; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0; anchors.centerIn: parent; width: 36; height: 36; avatar: true; source: app.api.profile?.avatar_url || ""; visible: app.page !== 4 }
                    AndroidGlyph { anchors.centerIn: parent; name: "settings"; visible: app.page === 4 }
                }
                background: Rectangle { radius: 24; color: accountButton.down ? "#e9e2e7" : "transparent"; Behavior on color { ColorAnimation { duration: app.motion } } }
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
            visible: !!app.api.error; Layout.fillWidth: true
            implicitHeight: errorText.implicitHeight + 20
            color: "#ffe8ee"
            Label { id: errorText; anchors.fill: parent; anchors.margins: 10; text: app.api.error || ""; wrapMode: Text.Wrap; color: "#97233b" }
            TapHandler { onTapped: sc.clearError() }
        }
        ColumnLayout {
            visible: !sc.signedIn
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.margins: 28; spacing: 20
            Item { Layout.fillHeight: true }
            Image { source: "qrc:/qt/qml/SCrip/assets/blob.png"; Layout.preferredWidth: 120; Layout.preferredHeight: 120; Layout.alignment: Qt.AlignHCenter; fillMode: Image.PreserveAspectFit; Rectangle { anchors.fill: parent; anchors.margins: -8; radius: 32; color: app.accent; z: -1 } }
            Label { text: "Твоя музыка.\nВ твоём ритме."; font.pixelSize: 27; font.bold: true; color: app.ink; Layout.fillWidth: true; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            Label { text: app.api.authBusy ? "Заверши вход в браузере и вернись в SCrip" : "Любимые треки, новые находки и миксы SoundCloud"; color: app.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            Action { text: app.api.authBusy ? "Отменить вход" : "Войти в SoundCloud"; primary: true; Layout.fillWidth: true; onClicked: app.api.authBusy ? sc.cancelLogin() : sc.login() }
            Item { Layout.fillHeight: true }
        }
        RowLayout {
            visible: sc.signedIn && app.page === 2
            Layout.fillWidth: true; Layout.margins: 20
            TextField {
                id: search
                Layout.fillWidth: true; implicitHeight: 48
                placeholderText: "Треки, плейлисты, исполнители"
                color: app.ink; placeholderTextColor: "#77717a"; selectionColor: app.accent; selectedTextColor: "white"
                font.pixelSize: 14; leftPadding: 14
                background: Rectangle { radius: 16; color: search.activeFocus ? "#f3edf0" : "#eeebee"; border.width: search.activeFocus ? 1 : 0; border.color: Qt.alpha(app.accent, 0.45); Behavior on color { ColorAnimation { duration: app.motion } } }
                onAccepted: { Qt.inputMethod.hide(); app.refresh() }
            }
            Action { text: "Найти"; primary: true; onClicked: { Qt.inputMethod.hide(); app.refresh() } }
        }
        RowLayout {
            visible: sc.signedIn && (app.page === 2 || app.page === 3)
            Layout.leftMargin: 20; Layout.rightMargin: 20; Layout.bottomMargin: 12
            Repeater {
                model: app.page === 2 ? ["Треки", "Плейлисты", "Люди"] : ["Любимые", "Плейлисты", "История"]
                Action {
                    required property string modelData
                    required property int index
                    text: modelData; Layout.fillWidth: true
                    primary: app.page === 2 ? app.searchKind === ["tracks", "sets", "users"][index] : app.library === ["likes", "playlists", "history"][index]
                    onClicked: {
                        if (app.page === 2) app.searchKind = ["tracks", "sets", "users"][index]
                        else app.library = ["likes", "playlists", "history"][index]
                        app.refresh()
                    }
                }
            }
        }
        Action {
            visible: sc.signedIn && app.page === 3 && app.library === "playlists"
            text: "Создать плейлист"; Layout.leftMargin: 20; Layout.bottomMargin: 12
            onClicked: createPlaylist.open()
        }
        ScrollView {
            visible: sc.signedIn && app.page === 0
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            Column {
                width: parent.width; spacing: 16
                Repeater {
                    model: app.api.home || []
                    Column {
                        required property var modelData
                        width: parent.width; spacing: 10
                        Label { x: 18; width: parent.width - 36; text: parent.modelData.title; color: app.ink; font.pixelSize: 19; font.bold: true; wrapMode: Text.Wrap }
                        ListView {
                            id: shelf
                            property var songs: parent.modelData.tracks || []
                            width: parent.width; height: 174; orientation: ListView.Horizontal
                            leftMargin: 18; rightMargin: 18; spacing: 10; clip: true
                            model: songs
                            delegate: ItemDelegate {
                                required property var modelData
                                required property int index
                                width: 120; height: 174; padding: 0
                                background: Item {}
                                contentItem: Column {
                                    spacing: 7
                                    Rectangle {
                                        width: 120; height: 120; radius: app.coverRadius; color: "#eee7ea"; clip: true
                                        RoundedImage { radius: app.coverRadius; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0; anchors.fill: parent; source: app.cover(modelData) }
                                    }
                                    Caption { width: 120; text: modelData.title || ""; font.bold: true; font.pixelSize: 13 }
                                    Caption { width: 120; text: modelData.user?.username || ""; color: app.muted; font.pixelSize: 12 }
                                }
                                onClicked: sc.playMix(shelf.songs, index)
                            }
                        }
                    }
                }
                Label { x: 18; width: parent.width - 36; visible: !(app.api.home || []).length; text: app.api.homeBusy ? "Собираем твою музыку…" : app.api.homeError || "Добавь любимые треки, чтобы здесь появились подборки"; wrapMode: Text.Wrap; color: app.muted }
                Action { x: 24; text: "Обновить подборки"; enabled: !app.api.homeBusy; onClicked: sc.loadHome(true) }
                PluginPanels { x: 18; width: parent.width - 36; manager: pluginManager; slotName: "home"; onReport: error => app.message = error }
                Item { width: 1; height: navigationFooter.height + 16 }
            }
        }
        ScrollView {
            visible: app.page === 5 || app.page === 6
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 18
                Action { text: "Назад"; symbol: "back"; Layout.leftMargin: 20; onClicked: app.page = app.page === 6 ? 5 : 4 }
                ColumnLayout {
                    visible: app.page === 5
                    Layout.fillWidth: true; Layout.margins: 18; spacing: 14
                    Caption { text: "Воспроизведение"; font.bold: true; font.pixelSize: 19 }
                    CheckBox { text: "Плавный переход между треками (3 секунды)"; checked: !!app.api.crossfade; Layout.fillWidth: true; onToggled: sc.setCrossfade(checked) }
                    Caption { text: "Виджет"; font.bold: true; font.pixelSize: 19 }
                    Caption { text: "Стиль для всех виджетов SCrip"; color: app.muted; font.pixelSize: 12 }
                    Repeater {
                        model: ["Blur Cover", "Dark Card", "Light Card"]
                        delegate: AbstractButton {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            implicitHeight: 76
                            Accessible.name: "Стиль виджета: " + modelData
                            background: Rectangle {
                                radius: 16
                                color: index === 2 ? "#f5f1f2" : index === 1 ? "#191a1d" : "#3a3438"
                                border.width: prefs.widgetStyle === index ? 2 : 1
                                border.color: prefs.widgetStyle === index ? app.accent : (index === 2 ? "#e6dfe2" : "#39383c")
                            }
                            contentItem: RowLayout {
                                spacing: 12
                                Rectangle {
                                    Layout.leftMargin: 10; Layout.preferredWidth: 48; Layout.preferredHeight: 48
                                    radius: 10; color: index === 2 ? "#d8d1d4" : index === 1 ? "#303237" : "#83616b"
                                    Image { anchors.fill: parent; anchors.margins: 2; source: "qrc:/qt/qml/SCrip/assets/blob.png"; fillMode: Image.PreserveAspectFit }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 3
                                    Text { text: modelData; color: index === 2 ? "#242126" : "#fffaf9"; font.pixelSize: 14; font.bold: true }
                                    Text { text: "Название трека · Исполнитель"; color: index === 2 ? "#77737a" : "#c8c3c8"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                Text { Layout.rightMargin: 14; text: prefs.widgetStyle === index ? "✓" : "▶   ❚❚"; color: index === 2 ? "#242126" : "#fffaf9"; font.pixelSize: 16 }
                            }
                            onClicked: prefs.widgetStyle = index
                        }
                    }
                    Caption { text: "SCrip"; font.bold: true; font.pixelSize: 19 }
                    Caption { text: "Версия " + Qt.application.version + (app.api.updateAvailable ? " · доступна " + app.api.latestVersion : ""); color: app.api.updateAvailable ? app.accent : app.muted; font.pixelSize: 13 }
                    Caption { text: app.api.updateError || app.api.updateResult || (app.api.updateChecking ? "Проверяем обновления…" : "Проверка ещё не выполнялась"); color: app.api.updateError ? "#a22c39" : app.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Caption { visible: !!app.api.updateDownloading; text: app.api.updateTotal > 0 ? (Number(app.api.updateDownloaded / 1048576).toFixed(1) + " МБ / " + Number(app.api.updateTotal / 1048576).toFixed(1) + " МБ · " + Math.floor(100 * app.api.updateDownloaded / app.api.updateTotal) + "%") : (app.api.updateDownloaded + " байт скачано"); color: app.muted; Layout.fillWidth: true }
                    ProgressBar { visible: !!app.api.updateDownloading; Layout.fillWidth: true; indeterminate: !(app.api.updateTotal > 0); from: 0; to: 100; value: app.api.updateTotal > 0 ? 100 * app.api.updateDownloaded / app.api.updateTotal : 0 }
                    Action { text: "Проверить обновления"; visible: !app.api.updateChecking && !app.api.updateDownloading && !app.api.updateInstalling && !app.api.updateReady; enabled: !app.api.updateChecking; Layout.fillWidth: true; onClicked: sc.checkForUpdates(true) }
                    Action { text: "Скачать обновление"; visible: !!app.api.updateAvailable && !app.api.updateDownloading && !app.api.updateReady && !app.api.updateInstalling; Layout.fillWidth: true; onClicked: sc.downloadUpdate() }
                    Action { text: "Отмена"; visible: !!app.api.updateDownloading; Layout.fillWidth: true; onClicked: sc.cancelUpdateDownload() }
                    Action { text: "Установить сейчас"; visible: !!app.api.updateReady && !app.api.updateInstalling; Layout.fillWidth: true; onClicked: sc.installUpdate() }
                    Caption { text: "Оформление"; font.bold: true; font.pixelSize: 19 }
                    Row {
                        spacing: 14
                        Repeater {
                            model: ["#f43756", "#7554ba", "#238579", "#2777c7", "#242426"]
                            AbstractButton {
                                required property string modelData
                                width: 42; height: 42; Accessible.name: "Цвет " + modelData
                                background: Rectangle { color: modelData; radius: 21; border.width: prefs.accent === modelData ? 4 : 0; border.color: "#d6cbd2" }
                                onClicked: { prefs.accent = modelData; prefs.accentOverride = true }
                            }
                        }
                    }
                    Caption { text: "Скругление обложек"; color: app.muted }
                    FineSlider { Layout.fillWidth: true; from: 0; to: 28; value: prefs.coverRadius; onMoved: { prefs.coverRadius = value; prefs.coverRadiusOverride = true } Accessible.name: "Скругление обложек" }
                    Action { text: "Использовать оформление плагинов"; onClicked: { prefs.accentOverride = false; prefs.coverRadiusOverride = false } }
                    Switch { text: "Анимации"; checked: prefs.motion; onToggled: prefs.motion = checked }
                    Switch { text: "Загружать треки при прокрутке"; checked: prefs.autoLoad; onToggled: prefs.autoLoad = checked }
                    Action { text: "Плагины"; Layout.fillWidth: true; onClicked: app.page = 6 }
                    Action { visible: Qt.platform.os === "android"; text: "Виджет на рабочий стол"; Layout.fillWidth: true; onClicked: app.message = androidMedia.addWidget() ? "Подтверди добавление виджета на рабочий стол" : "Удерживай свободное место на рабочем столе → Виджеты → SCrip" }
                    Label { text: app.message; visible: !!text && app.page === 5; wrapMode: Text.Wrap; color: app.muted; Layout.fillWidth: true }
                    Caption { text: "Аккаунт"; font.bold: true; font.pixelSize: 19 }
                    Action { text: sc.signedIn ? "Выйти из SoundCloud" : "Войти в SoundCloud"; Layout.fillWidth: true; onClicked: sc.signedIn ? sc.logout() : sc.login() }
                    Caption { text: "SCrip " + Qt.application.version + " · Android"; color: app.muted; font.pixelSize: 12 }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.margins: 18
                    PluginManagerView { Layout.fillWidth: true; manager: pluginManager; appearanceHint: prefs.accentOverride || prefs.coverRadiusOverride ? "Пользовательское оформление перекрывает соответствующие свойства темы плагина." : "" }
                }
                Item { Layout.fillWidth: true; implicitHeight: navigationFooter.height }
            }
        }
        ListView {
            id: tracks
            bottomMargin: navigationFooter.height
            visible: sc.signedIn && app.page > 0 && app.page < 5
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; model: app.api.tracks || []
            onContentYChanged: if (prefs.autoLoad && app.api.hasMore && !app.api.busy && contentY + height >= contentHeight - 180) sc.more()
            header: Column {
                width: tracks.width
                visible: app.page === 4
                height: visible ? implicitHeight : 0
                spacing: 14
                Rectangle {
                    width: parent.width; height: 156
                    gradient: Gradient { GradientStop { position: 0; color: Qt.tint("#f7edf2", Qt.alpha(app.accent, 0.15)) } GradientStop { position: 1; color: "#faf9fa" } }
                    Column {
                        anchors.centerIn: parent; spacing: 10
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter; width: 72; height: 72; radius: 36; color: "#eee5eb"; clip: true
                            RoundedImage { radius: app.coverRadius; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0; anchors.fill: parent; source: app.profile?.avatar_url || ""; avatar: true }
                        }
                        Caption { anchors.horizontalCenter: parent.horizontalCenter; text: app.profile?.username || "Твой профиль"; font.pixelSize: 19; font.bold: true }
                        Caption { anchors.horizontalCenter: parent.horizontalCenter; text: (app.profile?.followers_count || 0) + " подписчиков · " + (app.profile?.track_count || 0) + " треков"; color: app.muted; font.pixelSize: 12 }
                    }
                }
                Label { x: 18; width: parent.width - 36; text: app.profile?.description || ""; visible: !!text; wrapMode: Text.Wrap; color: app.muted; maximumLineCount: 4; elide: Text.ElideRight }
                RowLayout {
                    width: parent.width - 32; x: 16
                    Repeater {
                        model: app.guest ? ["Треки", "Плейлисты"] : ["Треки", "Плейлисты", "Лайки", "Репосты"]
                        Action { required property int index; required property string modelData; text: modelData; Layout.fillWidth: true; font.pixelSize: 12; leftPadding: 10; rightPadding: 10; implicitHeight: 44; primary: app.profileTab === index; onClicked: { app.profileTab = index; app.refresh() } }
                    }
                }
                PluginPanels { x: 18; width: parent.width - 36; manager: pluginManager; slotName: "profile"; onReport: error => app.message = error }
                Item { height: 8; width: 1 }
            }
            delegate: Track {}
            Label { anchors.centerIn: parent; width: parent.width - 48; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; visible: !tracks.count; color: app.muted; text: app.api.busy ? "Загрузка…" : app.page === 2 ? "Найди то, что хочется услышать" : "Здесь пока нет треков" }
            footer: Action { visible: !!app.api.hasMore; text: "Показать ещё"; enabled: !app.api.busy; onClicked: sc.more() }
        }
    }

    Item {
        id: navigationFooter
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
        Rectangle {
            id: miniPlayer
            visible: !!app.api.current?.title
            x: 16; width: parent.width - 32; height: 64; radius: 20; color: "#f0e9ed"
            TapHandler { onTapped: player.open() }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8; spacing: 10
                RoundedImage { Layout.preferredWidth: 44; Layout.preferredHeight: 44; source: app.cover(app.api.current); radius: 10; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0 }
                ColumnLayout {
                    Layout.fillWidth: true
                    Caption { text: app.api.current?.title || ""; Layout.fillWidth: true; font.bold: true }
                    Caption { text: app.api.current?.user?.username || ""; Layout.fillWidth: true; color: app.muted; font.pixelSize: 12 }
                }
                PlayerButton { text: app.api.playing ? "Пауза" : "Воспроизвести"; symbol: app.api.playing ? "pause" : "play"; Accessible.name: app.api.playing ? "Пауза" : "Воспроизвести"; onClicked: sc.togglePlayback() }
                PlayerButton { text: "Следующий"; symbol: "next"; Accessible.name: "Следующий трек"; onClicked: sc.next() }
            }
            Rectangle {
                anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 40; height: 2; radius: 1; color: "#e3d9df"
                Rectangle { height: parent.height; radius: 1; width: parent.width * Math.min(1, (sc.position || 0) / Math.max(1, sc.duration || 0)); color: app.accent }
            }
        }
        Item {
            width: parent.width
            height: 80
            RowLayout {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 16; anchors.rightMargin: 16
                height: 62; spacing: 10
                Rectangle {
                    id: navigationCapsule
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: height / 2
                    color: "#ffffff"
                    border.width: 1
                    border.color: "#f0ebef"
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
                        color: "#f2edf0"
                        opacity: navigationCapsule.selected >= 0 ? 1 : 0
                        Behavior on x { NumberAnimation { duration: app.motion; easing.type: Easing.OutQuint } }
                        Behavior on opacity { NumberAnimation { duration: app.motion / 2 } }
                    }
                    Row {
                        anchors.fill: parent; anchors.margins: 5
                        Repeater {
                            model: [
                                { title: "Главная", symbol: "home", page: 0 },
                                { title: "Лента", symbol: "list", page: 1 },
                                { title: "Медиатека", symbol: "music", page: 3 }
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
                                Behavior on scale { NumberAnimation { duration: prefs.motion ? 150 : 0; easing.type: Easing.OutCubic } }
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
                                        font.pixelSize: 10; font.bold: navigationTab.selected
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
                    Accessible.name: "Поиск"
                    scale: down ? 0.95 : 1
                    Behavior on scale { NumberAnimation { duration: prefs.motion ? 150 : 0; easing.type: Easing.OutCubic } }
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
                        AndroidGlyph { anchors.centerIn: parent; name: "search"; width: 25; height: 25; ink: "white" }
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
        parent: Overlay.overlay
        width: parent.width; height: parent.height
        modal: true; padding: 20
        topPadding: 12 + app.safeAreaTop(parent)
        bottomPadding: 16 + app.safeAreaBottom(parent)
        background: Rectangle { color: "#faf4f6" }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Action { text: "Свернуть"; symbol: "back"; Layout.preferredWidth: 48; Accessible.name: "Свернуть плеер"; onClicked: player.close() }
                Action { text: "Дополнения плеера"; symbol: "grid"; Layout.preferredWidth: 48; visible: pluginManager.panels.some(p => p.page === "player"); onClicked: pluginTools.open() }
                Caption { text: "СЕЙЧАС ИГРАЕТ"; font.pixelSize: 11; font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                Action { text: "Действия"; symbol: "more"; Layout.preferredWidth: 48; Accessible.name: "Действия с треком"; onClicked: { app.selectedTrack = app.api.current; app.selectedQueue = -1; trackMenu.open() } }
            }
            Item { Layout.fillHeight: true }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.max(80, Math.min(player.availableWidth, player.availableHeight - 330))
                Layout.preferredHeight: width
                color: "#ece2e7"; radius: 24; clip: true
                RoundedImage { anchors.fill: parent; source: app.cover(app.api.current); radius: 24; placeholderInk: app.muted; fadeDuration: prefs.motion ? 150 : 0 }
            }
            Item { Layout.fillHeight: true }
            Label { text: app.api.current?.title || ""; color: app.ink; font.bold: true; font.pixelSize: 21; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight; Layout.fillWidth: true }
            Caption { text: app.api.current?.user?.username || ""; color: app.muted; Layout.fillWidth: true }
            FineSlider {
                id: progress
                Layout.fillWidth: true; to: Math.max(1, sc.duration || 0)
                value: sc.position || 0
                enabled: (sc.duration || 0) > 0
                onPressedChanged: if (!pressed) sc.seek(value)
                Accessible.name: "Позиция воспроизведения"
            }
            RowLayout {
                Layout.fillWidth: true
                Caption { text: app.time(sc.position); color: app.muted; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Caption { text: app.time(sc.duration); color: app.muted; font.pixelSize: 12 }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 0
                PlayerButton { text: "Перемешать"; symbol: "shuffle"; active: !!app.api.shuffle; onClicked: sc.setShuffle(!app.api.shuffle) }
                Item { Layout.fillWidth: true }
                PlayerButton { text: "Предыдущий трек"; symbol: "previous"; onClicked: sc.previous() }
                Item { Layout.fillWidth: true }
                PlayerButton { text: app.api.playing ? "Пауза" : "Воспроизвести"; symbol: app.api.playing ? "pause" : "play"; main: true; onClicked: sc.togglePlayback() }
                Item { Layout.fillWidth: true }
                PlayerButton { text: "Следующий трек"; symbol: "next"; onClicked: sc.next() }
                Item { Layout.fillWidth: true }
                PlayerButton { text: "Повтор"; symbol: "repeat"; active: !!app.api.repeat; onClicked: sc.setRepeat(!app.api.repeat) }
            }
            RowLayout {
                Layout.fillWidth: true
                Action { text: app.api.current?.user_favorite ? "♥ В любимых" : "♡ Нравится"; Layout.fillWidth: true; onClicked: sc.like(app.api.current) }
                Action { text: "Очередь"; Layout.fillWidth: true; onClicked: queue.open() }
            }
        }
    }
    Sheet {
        id: queue
        parent: Overlay.overlay; width: parent.width; height: parent.height * 0.8; y: parent.height - height
        modal: true; padding: 0
        background: Rectangle { color: "#faf9fa"; radius: 24 }
        contentItem: ColumnLayout {
            RowLayout {
                Layout.margins: 20; Layout.fillWidth: true
                Caption { text: "Очередь"; font.pixelSize: 19; font.bold: true; Layout.fillWidth: true }
                Action { text: "Готово"; onClicked: queue.close() }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: app.api.queue || []
                delegate: Track { required property int index; queued: true; queueIndex: index }
            }
        }
    }
    Sheet {
        id: trackMenu
        parent: Overlay.overlay; width: parent.width; y: parent.height - height; modal: true; padding: 20
        background: Rectangle { color: "white"; radius: 24 }
        contentItem: ColumnLayout {
            Caption { text: app.selectedTrack.title || ""; font.bold: true; Layout.fillWidth: true }
            Action { text: "♡ Нравится / снять лайк"; Layout.fillWidth: true; onClicked: { sc.like(app.selectedTrack); trackMenu.close() } }
            Action { visible: app.selectedQueue >= 0; text: "Играть следующим"; Layout.fillWidth: true; onClicked: { sc.queueNext(app.selectedQueue); trackMenu.close() } }
            Action { visible: app.selectedQueue >= 0; text: "Удалить из очереди"; Layout.fillWidth: true; onClicked: { sc.removeQueue(app.selectedQueue); trackMenu.close() } }
            Action { text: "Добавить в очередь"; Layout.fillWidth: true; onClicked: { sc.enqueue(app.selectedTrack); trackMenu.close() } }
            Action { text: "Сохранить в плейлист"; Layout.fillWidth: true; onClicked: { sc.listDestinations(); destinations.open(); trackMenu.close() } }
            Action { text: "Профиль исполнителя"; Layout.fillWidth: true; onClicked: { app.openProfile(app.selectedTrack.user); trackMenu.close(); player.close() } }
            Action { text: "Станция по треку"; Layout.fillWidth: true; onClicked: { sc.playStation(app.selectedTrack); trackMenu.close() } }
            Action { text: "Открыть в SoundCloud"; Layout.fillWidth: true; onClicked: { sc.openExternal(app.selectedTrack.permalink_url); trackMenu.close() } }
            Action { text: "Копировать название и исполнителя"; Layout.fillWidth: true; onClicked: { sc.copyText((app.selectedTrack.title || "Без названия") + " — " + (app.selectedTrack.user?.username || "SoundCloud")); trackMenu.close() } }
            Action { text: "Копировать ссылку"; enabled: /^https:\/\/([a-z0-9-]+\.)*soundcloud\.com\//i.test(app.selectedTrack.permalink_url || ""); Layout.fillWidth: true; onClicked: { sc.copyText(app.selectedTrack.permalink_url); trackMenu.close() } }
            Action { text: "Закрыть"; Layout.fillWidth: true; onClicked: trackMenu.close() }
        }
    }
    Sheet {
        id: createPlaylist
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Math.max(16 + app.safeAreaTop(parent), (parent.height - Qt.inputMethod.keyboardRectangle.height - height) / 2)
        width: Math.min(parent.width - 32, 380)
        modal: true; padding: 20
        background: Rectangle { radius: 24; color: "#faf9fa" }
        onOpened: { playlistTitle.clear(); playlistTitle.forceActiveFocus() }
        onClosed: Qt.inputMethod.hide()
        function submit() {
            if (!playlistTitle.text.trim()) return
            sc.createPlaylist(playlistTitle.text.trim(), false)
            close()
        }
        contentItem: ColumnLayout {
            spacing: 16
            Caption { text: "Новый плейлист"; font.pixelSize: 20; font.bold: true; Layout.fillWidth: true }
            TextField {
                id: playlistTitle
                Layout.fillWidth: true; implicitHeight: 48
                placeholderText: "Название плейлиста"; maximumLength: 200
                font.pixelSize: 14; color: app.ink; placeholderTextColor: "#77717a"
                selectionColor: app.accent; selectedTextColor: "white"; leftPadding: 14
                background: Rectangle { radius: 16; color: "#eeebee"; border.width: playlistTitle.activeFocus ? 1 : 0; border.color: Qt.alpha(app.accent, 0.5) }
                onAccepted: createPlaylist.submit()
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Action { text: "Отмена"; Layout.fillWidth: true; onClicked: createPlaylist.close() }
                Action { text: "Создать"; primary: true; Layout.fillWidth: true; enabled: !!playlistTitle.text.trim(); onClicked: createPlaylist.submit() }
            }
        }
    }
    Sheet {
        id: destinations
        parent: Overlay.overlay; width: parent.width; height: parent.height * 0.7; y: parent.height - height
        modal: true; padding: 20
        background: Rectangle { radius: 24; color: "white" }
        contentItem: ColumnLayout {
            Caption { text: "Сохранить в плейлист"; font.bold: true; font.pixelSize: 22 }
            Label { visible: !!app.api.loadingDestinations; text: "Загрузка…"; color: app.muted }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: app.api.destinations || []
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width; height: 56; text: modelData.title
                    onClicked: { sc.saveToPlaylist(app.selectedTrack.urn, modelData.urn, ""); destinations.close() }
                }
                footer: Action { visible: !!app.api.destinationNext; text: "Ещё"; onClicked: sc.listDestinations(app.api.destinationNext) }
            }
            Action { text: "Закрыть"; Layout.fillWidth: true; onClicked: destinations.close() }
        }
    }
    Sheet {
        id: account
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(parent.width - 40, 380); modal: true; padding: 24
        background: Rectangle { color: "white"; radius: 24 }
        contentItem: ColumnLayout {
            Caption { text: app.api.profile?.username || "SCrip"; font.pixelSize: 19; font.bold: true; Layout.fillWidth: true }
            Label { text: "SoundCloud для твоего ритма"; color: app.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Action { text: sc.signedIn ? "Выйти из аккаунта" : "Войти в SoundCloud"; Layout.fillWidth: true; onClicked: { sc.signedIn ? sc.logout() : sc.login(); account.close() } }
            Action { text: "Закрыть"; Layout.fillWidth: true; onClicked: account.close() }
        }
    }
    PluginToolsPopup { id: pluginTools; manager: pluginManager }
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
        function onSignedInChanged() { app.page = 0; if (sc.signedIn) sc.loadHome(); else { player.close(); queue.close() } }
    }
    Sheet {
        id: startup
        parent: Overlay.overlay
        width: parent.width; height: parent.height
        modal: true; closePolicy: Popup.NoAutoClose; padding: 0
        background: Rectangle { color: "#faf9fa" }
        contentItem: Item {
            Column {
                anchors.centerIn: parent; spacing: 14
                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: "qrc:/qt/qml/SCrip/assets/blob.png"
                    width: 88; height: 88; fillMode: Image.PreserveAspectFit
                    Rectangle { anchors.fill: parent; anchors.margins: -16; radius: 32; color: app.accent; z: -1 }

                }
                Caption { anchors.horizontalCenter: parent.horizontalCenter; text: "SCrip"; font.pixelSize: 25; font.bold: true }
                Caption { anchors.horizontalCenter: parent.horizontalCenter; text: "Твоя музыка рядом"; color: app.muted; font.pixelSize: 13 }
            }
        }
        Timer { interval: 900; running: startup.opened; onTriggered: startup.close() }
    }
    Component.onCompleted: { startup.open(); if (sc.signedIn) sc.loadHome() }
}
