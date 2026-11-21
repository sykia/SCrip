import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: popup
    SwipeDismiss { targetPopup: popup; enabled: popup.mobile;  }
    readonly property int motionDuration: parent && parent.Window.window && parent.Window.window.motion !== undefined ? parent.Window.window.motion : 240
    opacity: 0; scale: 0.985
    enter: SurfaceTransition { duration: popup.motionDuration }
    exit: SurfaceTransition { duration: popup.motionDuration; leaving: true }
    required property var backend
    property bool mobile: Qt.platform.os === "android"
    property color ink: "#eeeeee"
    property color surface: "#292929"
    property color accent: "#f43756"
    property color selectedInk: "white"
    readonly property var api: backend.uiState
    readonly property var entries: (api.recommendationExclusions || []).filter(entry =>
        entry.name.toLowerCase().includes(search.text.toLowerCase()) || entry.urn.toLowerCase().includes(search.text.toLowerCase()))
    QtObject {
        id: appearance
        property color ink: popup.ink
        property color raised: Qt.alpha(popup.ink, 0.07)
        property color accent: popup.accent
        property bool dark: popup.ink.hslLightness > 0.5
        property int motion: 150
        function foreground(color) { return popup.selectedInk }
    }
    parent: Overlay.overlay
    width: Math.min(parent ? parent.width - (mobile ? 0 : 32) : 400, mobile ? 600 : 460)
    height: Math.min(parent ? parent.height - Math.max(48, safeTop) : 500, content.implicitHeight + topPadding + bottomPadding)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (mobile ? parent.height - height : (parent.height - height) / 2) : 0
    readonly property real safeTop: mobile && typeof SafeArea !== "undefined" && parent ? parent.SafeArea.margins.top : 0
    readonly property real safeBottom: mobile && typeof SafeArea !== "undefined" && parent ? parent.SafeArea.margins.bottom : 0
    padding: 20
    bottomPadding: mobile ? 20 + safeBottom : 20
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: popup.surface; radius: 22; border.color: Qt.alpha(popup.ink, 0.08) }
    Overlay.modal: Rectangle { color: "#88000000" }
    onOpened: search.text = ""
    Shortcut {
        sequence: "Back"
        enabled: popup.visible
        onActivated: {
            if (confirmation.visible) confirmation.close()
            else popup.close()
        }
    }
    contentItem: ColumnLayout {
        id: content
        spacing: 12
        Keys.onReleased: event => { if (event.key === Qt.Key_Back) { popup.close(); event.accepted = true } }
        RowLayout {
            Layout.fillWidth: true
            MetadataLabel { text: qsTr("Исключения рекомендаций"); color: popup.ink; font.pixelSize: 18; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
            SurfaceButton {
                host: appearance; mobile: true; quiet: true; implicitWidth: 44;
                text: popup.mobile ? "↺" : "⋯"
                Accessible.name: popup.mobile ? qsTr("Сбросить все исключения") : qsTr("Дополнительные действия")
                enabled: !popup.mobile || (popup.api.recommendationExclusions || []).length > 0
                onClicked: popup.mobile ? confirmation.open() : more.open()
                Menu {
                    id: more
                    MenuItem { text: qsTr("Сбросить все исключения"); enabled: (popup.api.recommendationExclusions || []).length > 0; onTriggered: confirmation.open() }
                }
            }
            SurfaceButton { host: appearance; mobile: true; quiet: true; implicitWidth: 44; text: "×"; Accessible.name: qsTr("Закрыть"); onClicked: popup.close() }
        }
        TextField {
            id: search; Layout.fillWidth: true
            implicitHeight: 44; leftPadding: 12; rightPadding: 12
            placeholderText: qsTr("Поиск"); Accessible.name: qsTr("Поиск исключений")
            color: popup.ink; placeholderTextColor: Qt.alpha(popup.ink, 0.6); selectionColor: popup.accent
            background: Rectangle { radius: 10; color: Qt.alpha(popup.ink, 0.07) }
        }
        MetadataLabel { visible: !(popup.api.recommendationExclusions || []).length; text: qsTr("Исключений пока нет"); color: popup.ink }
        MetadataLabel { visible: (popup.api.recommendationExclusions || []).length > 0 && !popup.entries.length; text: qsTr("Ничего не найдено"); color: popup.ink }
        ListView {
            visible: popup.entries.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(320, contentHeight)
            Layout.minimumHeight: 0
            clip: true
            model: popup.entries
            delegate: ColumnLayout {
                required property var modelData
                required property int index
                width: ListView.view.width
                spacing: 0
                MetadataLabel {
                    visible: index === 0 || popup.entries[index - 1].account !== modelData.account
                    text: modelData.account ? qsTr("Аккаунты") : qsTr("Треки")
                    color: popup.ink; font.bold: true
                    Layout.fillWidth: true; Layout.preferredHeight: visible ? 36 : 0
                    verticalAlignment: Text.AlignVCenter
                }
                MetadataLabel { text: modelData.name || modelData.urn; textFormat: Text.PlainText; color: popup.ink; wrapMode: Text.Wrap; Layout.fillWidth: true }
                SurfaceButton {
                    host: appearance; mobile: true; quiet: true
                    text: qsTr("Вернуть в рекомендации")
                    Accessible.name: text + ": " + (modelData.name || modelData.urn)
                    onClicked: popup.backend.restoreRecommendation(modelData.urn, modelData.account)
                    Layout.minimumHeight: 44
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }
    Popup {
        id: confirmation
        opacity: 0; scale: 0.985
        enter: SurfaceTransition { duration: popup.motionDuration }
        exit: SurfaceTransition { duration: popup.motionDuration; leaving: true }
        SwipeDismiss { targetPopup: confirmation; enabled: popup.mobile }
        parent: Overlay.overlay
        modal: true; focus: true
        width: Math.min(popup.width - 16, 360)
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        padding: 20
        background: Rectangle { radius: 22; color: popup.surface }
        contentItem: ColumnLayout {
            spacing: 16
            MetadataLabel { text: qsTr("Сбросить все исключения?"); color: popup.ink; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                SurfaceButton { host: appearance; mobile: true; text: qsTr("Отмена"); onClicked: confirmation.close() }
                SurfaceButton { host: appearance; mobile: true; text: qsTr("Сбросить"); destructive: true; onClicked: { popup.backend.clearRecommendationExclusions(); confirmation.close() } }
            }
        }
    }
}
