import QtQuick
import QtQuick.Controls.Basic as Basic
import ".." as Scrip

FocusScope {
    id: card
    required property var tokens
    property string title: ""
    property string subtitle: ""
    property string source: ""
    property string badge: ""
    property real coverRadius: 12
    property bool liked: false
    property bool likable: true
    property bool contributorLink: false
    signal contributorRequested()
    signal activated()
    signal likeRequested()
    signal enqueueRequested()
    implicitHeight: width + 56
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: title + " · " + subtitle
    Accessible.onPressAction: activated()
    Keys.onReturnPressed: activated()
    Keys.onSpacePressed: activated()
    HoverHandler { id: hover }
    Rectangle {
        anchors.fill: parent; radius: 20
        color: hover.hovered ? card.tokens.hover : card.tokens.elevated
        border.color: card.activeFocus ? card.tokens.accent : Qt.alpha(card.tokens.ink, hover.hovered ? 0.14 : 0.06)
        Behavior on color { ColorAnimation { duration: card.tokens.fast } }
    }
    Scrip.Artwork {
        x: 8; y: 8; width: parent.width - 16; height: width
        source: card.source; radius: card.coverRadius; placeholderColor: card.tokens.panel
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: card.activated() }
    Text { x: 14; y: card.width + 2; width: parent.width - 28; text: card.title; font.pixelSize: 14; font.weight: Font.DemiBold; color: card.tokens.ink; elide: Text.ElideRight }
    Text {
        x: 14; y: card.width + 24; width: parent.width - 28; text: card.subtitle
        font.pixelSize: 11; color: card.tokens.muted; elide: Text.ElideRight
        MouseArea { anchors.fill: parent; enabled: card.contributorLink; cursorShape: Qt.PointingHandCursor; onClicked: card.contributorRequested() }
    }
    Rectangle {
        visible: !!card.badge
        x: 16; y: 16; width: Math.min(label.implicitWidth + 16, card.width - 32); height: 23; radius: 7
        color: card.tokens.canvas
        Text { id: label; anchors.fill: parent; anchors.margins: 8; anchors.topMargin: 0; anchors.bottomMargin: 0; text: card.badge; color: card.tokens.ink; font.pixelSize: 9; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    }
    Row {
        x: 16; y: card.width - 52; spacing: 6
        visible: hover.hovered || card.activeFocus
        Repeater {
            model: card.likable ? ["play", "like", "add"] : ["play"]
            Basic.AbstractButton {
                id: action
                required property string modelData
                width: 34; height: 34
                Accessible.name: modelData === "play" ? "Слушать " + card.title : modelData === "like" ? "Нравится" : "В очередь"
                Accessible.checkable: modelData === "like"
                Accessible.checked: modelData === "like" && card.liked
                background: Rectangle { radius: 11; color: action.down ? card.tokens.pressed : card.tokens.canvas; border.color: Qt.alpha(card.tokens.ink, 0.12) }
                contentItem: Canvas {
                    property bool favorite: card.liked
                    onFavoriteChanged: requestPaint()
                    property color ink: card.tokens.ink
                    onInkChanged: requestPaint()
                    onPaint: {
                        const c = getContext("2d"); c.reset(); c.translate(8,8); c.scale(0.75,0.75)
                        c.strokeStyle = ink; c.fillStyle = ink; c.lineWidth = 1.7; c.lineCap = "round"; c.lineJoin = "round"; c.beginPath()
                        if (action.modelData === "play") { c.moveTo(7,4); c.lineTo(21,12); c.lineTo(7,20); c.closePath(); c.fill() }
                        else if (action.modelData === "add") { c.moveTo(12,4); c.lineTo(12,20); c.moveTo(4,12); c.lineTo(20,12); c.stroke() }
                        else { c.moveTo(12,21); c.bezierCurveTo(-5,10,5,-2,12,7); c.bezierCurveTo(19,-2,29,10,12,21); if (favorite) c.fill(); else c.stroke() }
                    }
                }
                onClicked: modelData === "play" ? card.activated() : modelData === "like" ? card.likeRequested() : card.enqueueRequested()
            }
        }
    }
}
