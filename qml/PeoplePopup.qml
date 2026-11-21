import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: popup
    SwipeDismiss {
        targetPopup: popup; enabled: popup.mobile
        entranceOffset: (1 - popup.reveal) * (popup.mobile ? popup.height : 16)
    }
    required property var host
    property bool mobile: false
    property var user: ({})
    property string kind: "followers"
    readonly property var state: sc.uiState
    signal profileRequested(var user)
    function begin(person, listKind) {
        user = person; kind = listKind
        sc.loadPeople(user, kind); sc.loadFollowings()
        open(); list.positionViewAtBeginning()
    }
    onClosed: sc.closePeople()
    parent: Overlay.overlay
    width: mobile ? parent.width : Math.min(520, parent.width - 64)
    height: Math.min(parent.height - (mobile ? 32 : 64), 650)
    x: (parent.width - width) / 2; y: mobile ? parent.height - height : (parent.height - height) / 2
    padding: mobile ? 20 : 24
    bottomPadding: padding + (mobile ? host.safeAreaBottom(host) : 0)
    property real reveal: 0
    readonly property int transitionDuration: host.motion > 0 ? Math.max(mobile ? 320 : 280, host.motion) : 0
    opacity: 0
    modal: true; focus: true
    background: Rectangle { radius: 22; color: popup.host.panel; border.color: popup.host.raised; border.width: popup.mobile ? 0 : 1 }
    Overlay.modal: Rectangle {
        color: "#88000000"
        Behavior on opacity { NumberAnimation { duration: popup.transitionDuration; easing.type: Easing.OutCubic } }
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 1; duration: popup.transitionDuration; easing.type: Easing.OutQuad }
            NumberAnimation { property: "reveal"; to: 1; duration: popup.transitionDuration; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: popup.transitionDuration * 0.8; easing.type: Easing.InOutCubic }
            NumberAnimation { property: "reveal"; to: 0; duration: popup.transitionDuration * 0.8; easing.type: Easing.InOutCubic }
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                MetadataLabel { Layout.fillWidth: true; text: popup.kind === "followers" ? qsTr("Подписчики") : popup.kind === "followings" ? qsTr("Подписки") : popup.kind === "favoriters" ? qsTr("Поставили лайк") : popup.kind === "reposters" ? qsTr("Сделали репост") : qsTr("Похожие исполнители"); color: popup.host.ink; font.pixelSize: 22; font.weight: Font.DemiBold; elide: Text.ElideRight }
                MetadataLabel { Layout.fillWidth: true; text: popup.user.title || popup.user.username || "SoundCloud"; textFormat: Text.PlainText; color: popup.host.muted; font.pixelSize: 12; elide: Text.ElideRight }
            }
            SurfaceButton { host: popup.host; mobile: popup.mobile; text: "×"; quiet: true; implicitWidth: 40; Accessible.name: qsTr("Закрыть список"); onClicked: popup.close() }
        }
        ListView {
            id: list
            objectName: "peopleList"
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: 0; Layout.preferredHeight: 0
            flickableDirection: Flickable.VerticalFlick
            SmoothWheel { flickable: list; duration: popup.host.motion > 0 ? 180 : 0 }
            clip: true; spacing: 4; boundsBehavior: Flickable.StopAtBounds
            model: popup.state.people || []
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: person
                required property var modelData
                width: list.width; height: popup.mobile ? 76 : 68
                leftPadding: 8; rightPadding: 8
                background: Rectangle { radius: 12; color: person.hovered || person.down ? popup.host.raised : "transparent" }
                onClicked: { popup.close(); popup.profileRequested(modelData) }
                contentItem: RowLayout {
                    spacing: 12
                    Artwork { Layout.preferredWidth: 44; Layout.preferredHeight: 44; radius: 22; source: person.modelData.avatar_url || "" }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        MetadataLabel { Layout.fillWidth: true; text: person.modelData.username || "SoundCloud"; textFormat: Text.PlainText; elide: Text.ElideRight; font.weight: Font.Medium; color: popup.host.ink }
                        MetadataLabel { Layout.fillWidth: true; text: Number(person.modelData.followers_count || 0).toLocaleString(Qt.locale(), "f", 0) + qsTr(" подписчиков"); font.pixelSize: 11; color: popup.host.muted; elide: Text.ElideRight }
                    }
                    SurfaceButton {
                        host: popup.host; mobile: popup.mobile
                        readonly property string urn: person.modelData.urn || ""
                        readonly property bool followed: !!popup.state.followings?.[urn]
                        visible: urn !== (popup.state.profile?.urn || "")
                        text: popup.state.followingPending?.[urn] ? "…" : followed ? qsTr("Вы подписаны") : popup.state.followingsLoading ? "…" : qsTr("Подписаться")
                        selected: followed
                        enabled: !popup.state.followingsLoading && !popup.state.followingPending?.[urn]
                        Accessible.name: (followed ? qsTr("Отписаться от ") : qsTr("Подписаться на ")) + person.modelData.username
                        onClicked: sc.toggleFollow(person.modelData)
                    }
                }
            }
            footer: Column {
                width: list.width; spacing: 12
                MetadataLabel { width: parent.width; topPadding: 24; text: popup.state.peopleError || (!list.count && !popup.state.peopleBusy ? (popup.kind === "related" ? qsTr("SoundCloud пока не предложил похожих исполнителей") : qsTr("Этот список пока пуст")) : ""); visible: !!text; color: popup.host.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
                BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: !!popup.state.peopleBusy; visible: running }
                SurfaceButton { anchors.horizontalCenter: parent.horizontalCenter; host: popup.host; mobile: popup.mobile; visible: !!popup.state.peopleError || !!popup.state.peopleHasMore; enabled: !popup.state.peopleBusy; text: popup.state.peopleError ? qsTr("Повторить") : qsTr("Показать ещё"); onClicked: sc.loadPeople(popup.user, popup.kind, !!popup.state.peopleHasMore) }
            }
        }
    }
}
