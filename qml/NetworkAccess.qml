import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Column {
    id: control
    required property var host
    property bool mobile: false
    property bool relayMode: false
    property var backend: typeof sc !== "undefined" ? sc : null
    property var route: typeof dnsRoute !== "undefined" ? dnsRoute : null
    readonly property bool countryNotice: !!control.host.api.connectionCountryBlocked && !(control.backend && control.backend.uiState.relayEnabled)
    function openRelayConsent() { relayMode = true; consent.open() }
    spacing: 8
    Rectangle {
        id: countryCard
        objectName: "countryAccessNotice"
        readonly property bool compact: control.mobile || width < 460
        property real reveal: control.countryNotice ? 1 : 0
        width: parent.width
        height: (noticeLayout.implicitHeight + 28) * reveal
        visible: height > 0
        opacity: reveal; clip: true
        radius: 12; color: control.host.panel
        border.width: 1
        border.color: control.host.line !== undefined ? control.host.line : Qt.alpha(control.host.ink, 0.1)
        Behavior on color { ColorAnimation { duration: control.host.motion } }
        Behavior on border.color { ColorAnimation { duration: control.host.motion } }
        Accessible.role: Accessible.AlertMessage
        Accessible.name: noticeTitle.text
        Accessible.description: noticeDescription.text
        Behavior on reveal { NumberAnimation { duration: control.host.motion; easing.type: Easing.OutCubic } }
        GridLayout {
            id: noticeLayout
            x: 14; y: 14; width: parent.width - 28
            columns: countryCard.compact ? 1 : 2
            columnSpacing: 20; rowSpacing: 12
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Label {
                    id: noticeTitle
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("SoundCloud недоступен в вашей стране")
                    color: control.host.ink; font.pixelSize: 13; font.weight: Font.DemiBold
                }
                Label {
                    id: noticeDescription
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Можно продолжить через сервер Sange.")
                    color: control.host.muted; font.pixelSize: 12
                }
            }
            SurfaceButton {
                objectName: "countryRelayConnect"
                host: control.host; mobile: control.mobile; primary: true
                text: qsTr("Подключить")
                enabled: !!control.backend && !control.backend.uiState.authBusy
                Layout.alignment: countryCard.compact ? Qt.AlignLeft : Qt.AlignVCenter | Qt.AlignRight
                onClicked: control.openRelayConsent()
            }
        }
    }
    Label {
        id: heading
        visible: !control.countryNotice
        width: parent.width
        text: control.backend && control.backend.uiState.relayEnabled ? qsTr("Подключение через сервер Sange") : control.host.api.connectionCountryBlocked ? qsTr("SoundCloud недоступен из вашей страны") : control.route && control.route.enabled ? qsTr("Защищённый DNS включён") : control.host.api.connectionIssue ? qsTr("SoundCloud недоступен в этой сети") : qsTr("SoundCloud не открывается?")
        color: control.host.muted; font.pixelSize: 12; wrapMode: Text.Wrap
    }
    Flow {
        id: actions
        visible: !control.countryNotice
        width: parent.width; spacing: 6
        SurfaceButton {
            objectName: "dnsToggle"
            host: control.host; mobile: control.mobile
            text: control.route && control.route.enabled ? qsTr("Отключить DNS") : qsTr("Использовать DNS")
            enabled: !!control.route
            visible: !control.host.api.connectionCountryBlocked || (control.route && control.route.enabled)
            onClicked: {
                if (control.route.enabled) control.route.setEnabled(false)
                else { control.relayMode = false; consent.open() }
            }
        }
        SurfaceButton {
            objectName: "relayToggle"
            host: control.host; mobile: control.mobile
            primary: !!control.host.api.connectionCountryBlocked && !(control.backend && control.backend.uiState.relayEnabled)
            text: control.backend && control.backend.uiState.relayEnabled ? qsTr("Отключить сервер") : qsTr("Использовать сервер")
            enabled: !!control.backend
            onClicked: {
                if (control.backend.uiState.relayEnabled) control.backend.setRelayEnabled(false)
                else { control.relayMode = true; consent.open() }
            }
        }
        SurfaceButton {
            host: control.host; mobile: control.mobile; quiet: true
            text: qsTr("Повторить")
            enabled: !!control.backend && !control.backend.uiState.busy && !control.backend.uiState.authBusy
            onClicked: control.backend.connectCatalog()
        }
    }
    Popup {
        id: consent
        objectName: "dnsConsent"
        parent: Overlay.overlay
        anchors.centerIn: parent
        property bool detailsOpen: false
        onClosed: detailsOpen = false
        width: Math.min(400, control.host.width - 32, parent.width - 32)
        height: Math.min(dialogColumn.implicitHeight + 48, parent.height - 32)
        padding: 24; modal: true; focus: true
        opacity: 0; scale: 0.985
        enter: SurfaceTransition { duration: control.host.motion }
        exit: SurfaceTransition { duration: control.host.motion; leaving: true }
        background: Rectangle {
            radius: 16; color: control.host.panel
            border.width: 1; border.color: Qt.alpha(control.host.ink, 0.07)
        }
        Overlay.modal: Rectangle { color: "#66000000" }
        contentItem: Flickable {
            contentHeight: dialogColumn.implicitHeight
            clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            ColumnLayout {
                id: dialogColumn
                width: parent.width; spacing: 0
                Label {
                    text: qsTr("ПОДКЛЮЧЕНИЕ"); color: control.host.muted
                    font.pixelSize: 10; font.weight: Font.Medium; font.letterSpacing: 1.4
                    Layout.bottomMargin: 10
                }
                Label {
                    text: control.relayMode ? qsTr("Сервер Sange") : qsTr("Защищённый DNS"); color: control.host.ink
                    font.pixelSize: 20; font.weight: Font.DemiBold
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                }
                Label {
                    text: control.relayMode ? qsTr("Другой маршрут к SoundCloud, когда доступ из вашей страны закрыт.") : qsTr("Попробуйте, если SoundCloud не открывается у вашего провайдера.")
                    color: control.host.muted; font.pixelSize: 13
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    Layout.topMargin: 8; Layout.bottomMargin: 20
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Qt.alpha(control.host.ink, 0.08) }
                RowLayout {
                    spacing: 12; Layout.fillWidth: true
                    Layout.topMargin: 14; Layout.bottomMargin: 14
                    Rectangle {
                        width: 34; height: 34; radius: 10
                        color: Qt.alpha(control.host.ink, 0.045)
                        Canvas {
                            anchors.centerIn: parent; width: 18; height: 18
                            property color ink: control.host.muted
                            onInkChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset(); ctx.strokeStyle = ink; ctx.lineWidth = 1.15
                                ctx.beginPath(); ctx.arc(9, 9, 7, 0, 2 * Math.PI); ctx.stroke()
                                ctx.beginPath(); ctx.ellipse(6, 2, 6, 14); ctx.stroke()
                                ctx.beginPath(); ctx.moveTo(2, 9); ctx.lineTo(16, 9); ctx.stroke()
                            }
                        }
                    }
                    ColumnLayout {
                        spacing: 3
                        Label { text: control.relayMode ? "Sange" : "Cloudflare"; color: control.host.ink; font.pixelSize: 13; font.weight: Font.Medium }
                        Label { text: control.relayMode ? qsTr("Сервер приложения") : "1.1.1.1"; color: control.host.muted; font.pixelSize: 12 }
                    }
                    Item { Layout.fillWidth: true }
                    Label { text: qsTr("Бесплатно"); color: control.host.muted; font.pixelSize: 11 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Qt.alpha(control.host.ink, 0.08) }
                Label {
                    text: control.relayMode ? qsTr("Сервер Sange получит ваш IP, запросы к API и токен доступа SoundCloud. Включайте, только если доверяете сервису.") : qsTr("Cloudflare увидит ваш IP и запрашиваемые домены. Помогает только при блокировке DNS.")
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    color: control.host.muted; font.pixelSize: 12
                    Layout.topMargin: 16; Layout.bottomMargin: 6
                }
                AbstractButton {
                    id: detailsToggle
                    objectName: "dnsDetails"
                    text: qsTr("Как это работает")
                    Accessible.name: text
                    Accessible.description: consent.detailsOpen ? qsTr("Свернуть подробности") : qsTr("Показать подробности")
                    implicitHeight: 30; implicitWidth: detailsRow.implicitWidth
                    hoverEnabled: true
                    onClicked: consent.detailsOpen = !consent.detailsOpen
                    contentItem: Row {
                        id: detailsRow; spacing: 6
                        Label { text: detailsToggle.text; color: detailsToggle.hovered ? control.host.ink : control.host.muted; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                        Label {
                            text: "›"; font.pixelSize: 16; color: control.host.muted
                            rotation: consent.detailsOpen ? 90 : 0
                            anchors.verticalCenter: parent.verticalCenter
                            Behavior on rotation { NumberAnimation { duration: control.host.motion * 0.65; easing.type: Easing.OutCubic } }
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: consent.detailsOpen ? detailText.implicitHeight + 10 : 0
                    clip: true
                    Behavior on implicitHeight { NumberAnimation { duration: control.host.motion; easing.type: Easing.OutCubic } }
                    Label {
                        id: detailText
                        width: parent.width; topPadding: 4
                        text: control.relayMode ? qsTr("Запросы к API и музыка пойдут по HTTPS через сервер Sange. Сервер получает аудио с CDN SoundCloud и передаёт его приложению. Токен нужен серверу для выполнения ваших запросов; он не записывается в журнал или общее хранилище.\n\nРежим не меняет VPN и не гарантирует доступность всех треков. При недоступности сервера приложение покажет ошибку. Отключить можно здесь или в настройках.") : qsTr("Адреса SoundCloud будут запрашиваться у Cloudflare по HTTPS. Соединение с SoundCloud останется зашифрованным. Блокировки IP и TLS этот режим не снимает.\n\nДействует только в Sange. Настройки системы, VPN и вход в браузере не меняются. Отключить можно в настройках приложения.")
                        color: control.host.muted; font.pixelSize: 12; wrapMode: Text.Wrap
                        opacity: consent.detailsOpen ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: control.host.motion * 0.65 } }
                    }
                }
                Label {
                    visible: !!control.route && !!control.route.error
                    text: control.route ? control.route.error : ""
                    color: control.host.ink; Layout.fillWidth: true; wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 20; spacing: 8
                    Item { Layout.fillWidth: true }
                    SurfaceButton {
                        objectName: "dnsDecline"; host: control.host; mobile: control.mobile; quiet: true
                        text: qsTr("Отмена"); onClicked: consent.close()
                    }
                    SurfaceButton {
                        objectName: "dnsAgree"; host: control.host; mobile: control.mobile; primary: true
                        text: control.relayMode ? qsTr("Подключить") : qsTr("Включить DNS")
                        onClicked: {
                            if (control.relayMode) {
                                control.backend.setRelayEnabled(true)
                                consent.close(); control.backend.connectCatalog()
                            } else {
                                control.route.setEnabled(true)
                                if (control.route.enabled) { consent.close(); control.backend.connectCatalog() }
                            }
                        }
                    }
                }
            }
        }
    }
}
