import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml"

Rectangle {
    id: host
    width: 420; height: 600
    property int motion: 0
    property bool dark: true
    property color panel: dark ? "#1a1a1a" : "#f7f7f8"
    property color raised: "#33333b"
    property color ink: "#f0edf5"
    property color muted: "#aaa5b2"
    property color accent: "#c3a2fa"
    property var api: ({connectionIssue: true})
    function foreground(color) { return "#24202a" }
    QtObject {
        id: route
        property bool enabled: false
        property string error: ""
        function setEnabled(value) { enabled = value }
    }
    QtObject {
        id: backend
        property var uiState: ({busy: false, authBusy: false})
        property int attempts: 0
        function connectCatalog() { ++attempts }
        function setRelayEnabled(enabled) { uiState = {busy: false, authBusy: false, relayEnabled: enabled} }
    }
    NetworkAccess { id: card; width: parent.width - 24; host: host; route: route; backend: backend }
    TestCase {
        name: "NetworkAccess"
        when: windowShown
        function init() {
            host.width = 420; host.dark = true; card.mobile = false
            host.api = {connectionIssue: true, connectionCountryBlocked: false}
            route.enabled = false; backend.attempts = 0
            backend.uiState = {busy: false, authBusy: false, relayEnabled: false}
        }
        function test_countryNotice_data() {
            return [
                {tag: "desktop-dark", size: 640, mobile: false, dark: true},
                {tag: "desktop-light", size: 640, mobile: false, dark: false},
                {tag: "android-dark", size: 320, mobile: true, dark: true},
                {tag: "android-light", size: 320, mobile: true, dark: false}
            ]
        }
        function test_countryNotice(data) {
            host.width = data.size; host.dark = data.dark; card.mobile = data.mobile
            const banner = findChild(card, "countryAccessNotice")
            compare(banner.visible, false)
            // A country result remains visible even if an unrelated OAuth response clears the generic hint.
            host.api = {connectionIssue: false, connectionCountryBlocked: true}
            tryCompare(banner, "visible", true)
            verify(banner.height > 0)
            compare(banner.color, host.panel)
            const connect = findChild(card, "countryRelayConnect")
            verify(connect.visible)
            verify(connect.width <= banner.width - 28)
            connect.clicked()
            const popup = findChild(card, "dnsConsent")
            tryCompare(popup, "opened", true)
            compare(card.relayMode, true)
            verify(!backend.uiState.relayEnabled)
            findChild(card, "dnsDecline").clicked()
            tryCompare(popup, "opened", false)
            verify(banner.visible)
            connect.clicked()
            tryCompare(popup, "opened", true)
            findChild(card, "dnsAgree").clicked()
            compare(backend.uiState.relayEnabled, true)
            compare(backend.attempts, 1)
            tryCompare(banner, "visible", false)
            tryCompare(popup, "opened", false)
        }
        function test_noticeFollowsTheme() {
            host.api = {connectionCountryBlocked: true}
            const banner = findChild(card, "countryAccessNotice")
            const connect = findChild(card, "countryRelayConnect")
            host.accent = "#2e8f70"
            compare(banner.color, host.panel)
            compare(connect.background.color, host.accent)
            host.dark = false
            compare(banner.color, host.panel)
            host.accent = "#245dc1"
            compare(connect.background.color, host.accent)
            compare(banner.color, host.panel)
        }
        function test_consentAndRevoke() {
            const toggle = findChild(card, "dnsToggle")
            const popup = findChild(card, "dnsConsent")
            toggle.clicked()
            tryCompare(popup, "opened", true)
            compare(route.enabled, false)
            findChild(card, "dnsDecline").clicked()
            tryCompare(popup, "opened", false)
            compare(route.enabled, false)
            toggle.clicked()
            tryCompare(popup, "opened", true)
            findChild(card, "dnsAgree").clicked()
            compare(route.enabled, true)
            compare(backend.attempts, 1)
            tryCompare(popup, "opened", false)
            toggle.clicked()
            compare(route.enabled, false)
        }
        function test_relayRequiresConsentAndCanBeRevoked() {
            host.width = 420
            const toggle = findChild(card, "relayToggle")
            const popup = findChild(card, "dnsConsent")
            const attempts = backend.attempts
            toggle.clicked()
            tryCompare(popup, "opened", true)
            verify(!backend.uiState.relayEnabled)
            findChild(card, "dnsAgree").clicked()
            compare(backend.uiState.relayEnabled, true)
            compare(backend.attempts, attempts + 1)
            tryCompare(popup, "opened", false)
            toggle.clicked()
            compare(backend.uiState.relayEnabled, false)
        }
        function test_narrowMobileLayout() {
            host.width = 320
            card.mobile = true
            findChild(card, "dnsToggle").clicked()
            const popup = findChild(card, "dnsConsent")
            tryCompare(popup, "opened", true)
            verify(popup.width <= host.width)
            verify(popup.height <= host.height)
            findChild(card, "dnsDetails").clicked()
            compare(popup.detailsOpen, true)
            verify(popup.height <= host.height)
            findChild(card, "dnsDecline").clicked()
            tryCompare(popup, "opened", false)
        }
    }
}
