import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var host
    property bool mobile: false
    property color ink: host.ink
    property color muted: host.muted
    spacing: 10
    Label {
        text: qsTr("Нашли ошибку?")
        color: root.ink
        font.pixelSize: 14
        font.weight: Font.DemiBold
        Layout.fillWidth: true
    }
    Label {
        text: qsTr("Скопируйте лог и приложите его к сообщению на GitHub. Напишите, что произошло и как повторить ошибку.")
        color: root.muted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        SurfaceButton {
            host: root.host; mobile: root.mobile
            objectName: "copyDiagnosticLog"
            text: qsTr("Скопировать последний лог")
            width: Math.min(implicitWidth, parent.width)
            onClicked: diagnostics.copyReport()
        }
        SurfaceButton {
            host: root.host; mobile: root.mobile
            objectName: "openDiagnosticIssue"
            text: qsTr("Открыть GitHub Issues ↗")
            width: Math.min(implicitWidth, parent.width)
            onClicked: diagnostics.openIssue()
        }
    }
    Label {
        text: typeof diagnostics !== "undefined" ? diagnostics.status : ""
        visible: text.length > 0
        color: root.muted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
    }
    Label {
        text: qsTr("Перед публикацией проверьте лог: отчёты на GitHub видны всем.")
        color: root.muted
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
