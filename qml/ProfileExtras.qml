import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Column {
    id: extras
    required property var host
    property var user: ({})
    property bool mobile: false
    property bool expanded: false
    readonly property string userUrn: user?.urn || ""
    readonly property var api: sc.uiState
    readonly property var links: api.profileLinksUser === userUrn ? api.profileLinks || [] : []
    signal relatedRequested()
    spacing: 12
    function loadLinks() { if (userUrn) sc.loadProfileLinks(user) }
    onUserUrnChanged: { expanded = false; Qt.callLater(extras.loadLinks) }
    Component.onCompleted: Qt.callLater(extras.loadLinks)
    Flow {
        width: parent.width; spacing: 8
        SurfaceButton { host: extras.host; mobile: extras.mobile; quiet: true; text: qsTr("Похожие исполнители"); visible: !!extras.userUrn; onClicked: extras.relatedRequested() }
        Repeater {
            model: extras.expanded ? extras.links : extras.links.slice(0, 3)
            SurfaceButton {
                required property var modelData
                host: extras.host; mobile: extras.mobile; quiet: true
                width: Math.min(implicitWidth, extras.width)
                text: modelData.title || modelData.service || modelData.username || modelData.url.replace(/^https?:\/\/(www\.)?/, "").split("/")[0]
                Accessible.name: qsTr("Открыть ") + text + ": " + modelData.url
                onClicked: sc.openProfileWebsite(modelData.url)
                ToolTip.visible: extras.host.showTooltips === true && (hovered); ToolTip.text: modelData.url
            }
        }
        SurfaceButton { host: extras.host; mobile: extras.mobile; quiet: true; visible: extras.links.length > 3; text: extras.expanded ? qsTr("Свернуть ссылки") : qsTr("Ещё ") + (extras.links.length - 3); onClicked: extras.expanded = !extras.expanded }
        SurfaceButton { host: extras.host; mobile: extras.mobile; visible: !!extras.api.profileLinksError; text: qsTr("Повторить загрузку ссылок"); onClicked: sc.loadProfileLinks(extras.user) }
        SurfaceButton { host: extras.host; mobile: extras.mobile; visible: !!extras.api.profileLinksHasMore; text: qsTr("Ещё ссылки"); enabled: !extras.api.profileLinksBusy; onClicked: sc.loadProfileLinks(extras.user, true) }
    }
}
