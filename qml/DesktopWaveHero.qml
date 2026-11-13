import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "components" as UI

Item {
    id: hero
    required property var api
    required property var backend
    required property var tokens
    property color accent
    property string artwork: ""
    property bool motion: true
    property bool inViewport: true
    property int quality: 1
    implicitHeight: 260

    Item {
        id: backdrop
        anchors.fill: parent
        Rectangle { anchors.fill: parent; radius: 24; color: hero.tokens.panel }
        Artwork { anchors.fill: parent; source: hero.artwork; radius: 24; opacity: hero.tokens.dark ? 0.28 : 0.18 }
    }
    UI.GlassSurface {
        anchors.fill: parent; radius: 24
        dark: hero.tokens.dark; color: hero.tokens.elevated
        backdropItem: backdrop; quality: hero.quality
        density: hero.tokens.dark ? 0.50 : 0.58
        refraction: 2.0
    }
    ColumnLayout {
        x: 24; y: 20; width: parent.width * 0.43 - 24; height: parent.height - 40
        spacing: 8
        Text { text: "ПЕРСОНАЛЬНЫЙ ЭФИР"; color: hero.tokens.muted; font.pixelSize: 10; font.letterSpacing: 1.8 }
        Text { text: "Своя волна"; color: hero.tokens.ink; font.pixelSize: hero.width < 650 ? 28 : 34; font.weight: Font.DemiBold; font.letterSpacing: -1; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Text { text: "Любимые треки и новые открытия"; color: hero.tokens.muted; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Repeater {
                model: [{key: "library", title: "Моя музыка"}, {key: "discovery", title: "Открытия"}]
                Basic.Button {
                    id: mode
                    required property var modelData
                    Layout.fillWidth: true; implicitHeight: 32
                    text: modelData.title
                    background: Rectangle { radius: 10; color: original.selectedMode === mode.modelData.key ? hero.tokens.hover : "transparent"; border.color: original.selectedMode === mode.modelData.key ? hero.tokens.line : "transparent" }
                    contentItem: Text { text: mode.text; color: hero.tokens.ink; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    onClicked: {
                        original.selectedMode = modelData.key
                        if (hero.api.waveMode && !original.active) hero.backend.startWave(original.selectedMode)
                    }
                }
            }
        }
        Basic.Button {
            id: listen
            objectName: "waveHeroListen"
            Layout.fillWidth: true; implicitHeight: 42
            text: original.active && hero.api.playbackActive ? "Приостановить" : original.active && hero.api.waveError ? "Повторить" : "Слушать волну"
            enabled: !(original.active && hero.api.waveBusy && !(hero.api.queue || []).length)
            Accessible.name: text
            onClicked: original.toggleWave()
            background: Rectangle { radius: 12; color: hero.tokens.ink; opacity: listen.down ? 0.8 : 1 }
            contentItem: Text { text: listen.text; color: hero.tokens.canvas; font.pixelSize: 13; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
        Text { text: hero.api.waveError || (hero.api.waveBusy ? "Ищем продолжение…" : "Подстраивается под твой вкус"); color: hero.api.waveError ? hero.tokens.errorInk : hero.tokens.muted; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
    }
    Item {
        id: stage
        x: parent.width * 0.45; width: parent.width - x - 8
        y: 0; height: parent.height
        clip: true
        // Present only the original visual viewport. No reparenting, shader edits,
        // scaling, time changes or audio changes are applied to WaveCard.
        property Item originalVisual: null
        function findVisual(item) {
            if (item.objectName === "waveVisual") return item
            for (const child of item.children) {
                const found = findVisual(child)
                if (found) return found
            }
            return null
        }
        WaveCard {
            id: original
            width: stage.width
            y: stage.originalVisual ? -stage.originalVisual.y + (stage.height - stage.originalVisual.height) / 2 : 0
            api: hero.api; backend: hero.backend; accent: hero.accent
            dark: hero.tokens.dark; motion: hero.motion; inViewport: hero.inViewport
            Component.onCompleted: {
                stage.originalVisual = stage.findVisual(original)
                // The desktop shell supplies its own heading and accessible mode controls.
                // Hide the original chrome instead of leaving clipped tab stops behind.
                if (stage.originalVisual) {
                    for (const child of stage.originalVisual.parent.children)
                        if (child !== stage.originalVisual) child.visible = false
                }
            }
        }
    }
}
