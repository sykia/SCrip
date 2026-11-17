import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: notice
    required property var backend
    property color ink: "#eeeeee"
    property color surface: "#292929"
    QtObject {
        id: appearance
        property color ink: notice.ink
        property color raised: Qt.alpha(notice.ink, 0.06)
        property int motion: 150
    }
    parent: Overlay.overlay
    width: Math.min(parent ? parent.width - 32 : 360, 420)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? parent.height - height - 40 : 0
    padding: 12
    closePolicy: Popup.NoAutoClose
    background: Rectangle { color: notice.surface; radius: 12; border.color: Qt.alpha(notice.ink, 0.15) }
    contentItem: RowLayout {
        Label { text: "Исключено из рекомендаций"; color: notice.ink; wrapMode: Text.Wrap; Layout.fillWidth: true }
        SurfaceButton { host: appearance; quiet: true; mobile: true; text: "Отменить"; Layout.minimumHeight: 44; onClicked: { notice.backend.undoRecommendationExclusion(); notice.close() } }
    }
    Timer { id: timeout; interval: 6000; onTriggered: notice.close() }
    Connections {
        target: notice.backend
        function onRecommendationExclusionAdded() { notice.open(); timeout.restart() }
        function onCatalogChanged() {
            if (!notice.backend.uiState.recommendationUndo) notice.close()
        }
    }
}
