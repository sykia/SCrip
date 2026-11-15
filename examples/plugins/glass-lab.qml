import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Author code: no predefined card or visual recipe is imposed by the host.
Item {
    id: extension
    required property var sdk
    objectName: "glassLabExtension"
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    z: 900
    property var dock: sdk.find("playbackDock")
    property bool expanded: false
    property int ticks: 0

    // Disable the override before destruction so Qt restores the previous binding.
    Binding {
        target: extension.dock
        property: "opacity"
        value: 0.65
        when: sdk.active && !!extension.dock && !!extension.sdk.settings["dim-dock"]
        restoreMode: Binding.RestoreBindingOrValue
    }
    Timer { interval: 1000; running: extension.expanded; repeat: true; onTriggered: extension.ticks++ }
    Rectangle {
        id: glass
        objectName: "glassLabMenu"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        anchors.topMargin: 72
        width: Math.min(300, Math.max(0, extension.width - 40))
        height: extension.expanded ? column.implicitHeight + 32 : 54
        radius: 24
        color: "#dd182833"
        border.color: "#70b8f5ed"
        clip: true
        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        // An independently authored, blurred gradient behind the controls.
        Item {
            id: pigment
            anchors.fill: parent
            visible: false
            Rectangle {
                x: -40; y: -40; width: 220; height: 160; radius: 80
                rotation: extension.ticks * 7
                gradient: Gradient {
                    GradientStop { position: 0; color: "#49dbc3" }
                    GradientStop { position: 1; color: "#383e83" }
                }
            }
        }
        MultiEffect {
            anchors.fill: pigment
            source: pigment
            blurEnabled: true
            blurMax: 32
            blur: 0.85
            opacity: 0.55
        }
        Rectangle {
            anchors.fill: parent; radius: parent.radius; color: "transparent"
            border.width: 1; border.color: "#60ffffff"
        }
        Column {
            id: column
            x: 16; y: 8; width: parent.width - 32; spacing: 8
            Button {
                width: parent.width; text: extension.expanded ? "Glass Lab · свернуть" : "Glass Lab · открыть"
                onClicked: extension.expanded = !extension.expanded
            }
            Column {
                visible: extension.expanded
                width: parent.width; spacing: 8
                Label { width: parent.width; text: "Авторское меню · " + extension.ticks + " с"; color: "white"; wrapMode: Text.Wrap }
                Button { width: parent.width; text: "Предыдущий трек"; onClicked: sdk.player.previous() }
                Button { width: parent.width; text: "Следующий трек"; onClicked: sdk.player.next() }
                Switch {
                    width: parent.width
                    text: "Приглушить плеер"
                    checked: !!sdk.settings["dim-dock"]
                    onToggled: sdk.setSetting("dim-dock", checked)
                }
            }
        }
    }
}
