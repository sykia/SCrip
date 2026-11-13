import QtQuick
import QtQuick.Effects

Item {
        id: rounded
        property alias source: picture.source
        property real radius: 16
        property color placeholderInk: "#858589"
        property int fadeDuration: 150
        property bool avatar: false
        Rectangle { anchors.fill: parent; radius: rounded.avatar ? width / 2 : rounded.radius; color: "#ededed" }
        AndroidGlyph { anchors.centerIn: parent; visible: rounded.avatar && picture.status !== Image.Ready; name: "profile"; ink: rounded.placeholderInk }
        Image {
            id: picture
            anchors.fill: parent
            asynchronous: true
            // Ограничиваем декодирование и текстуру физическим размером обложки.
            sourceSize.width: Math.max(1, Math.ceil(width * Screen.devicePixelRatio))
            sourceSize.height: Math.max(1, Math.ceil(height * Screen.devicePixelRatio))
            fillMode: Image.PreserveAspectCrop
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: mask
            }
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: rounded.fadeDuration } }
        }
        Item {
            id: mask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Rectangle { anchors.fill: parent; radius: rounded.avatar ? width / 2 : rounded.radius; color: "white" }
        }
}
