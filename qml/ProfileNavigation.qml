import QtQuick
import QtQuick.Controls

Item {
    id: navigation
    required property var host
    property bool mobile: false
    property int section: 0
    property string likes: "likes"
    property string reposts: "reposts"
    signal navigate(int section, string collection)
    property real indicatorX: 0
    property real indicatorWidth: 0
    function updateIndicator() {
        const selected = buttons.itemAt(section)
        if (selected) {
            indicatorX = selected.x; indicatorWidth = selected.width
            const right = selected.x + selected.width
            const maximum = Math.max(0, tabs.implicitWidth - viewport.width)
            let target = Math.max(0, Math.min(maximum, viewport.contentX))
            if (selected.x < target) target = selected.x
            else if (right > target + viewport.width) target = right - viewport.width
            viewport.contentX = Math.max(0, Math.min(maximum, target))
        }
    }
    onSectionChanged: Qt.callLater(updateIndicator)
    onWidthChanged: Qt.callLater(updateIndicator)
    Component.onCompleted: Qt.callLater(updateIndicator)
    implicitHeight: 44
    objectName: "profileNavigation"

    Flickable {
        id: viewport
        objectName: "profileTabsViewport"
        onWidthChanged: Qt.callLater(navigation.updateIndicator)
        onContentWidthChanged: Qt.callLater(navigation.updateIndicator)
        anchors.fill: parent; clip: true
        contentWidth: tabs.implicitWidth; contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        Behavior on contentX { enabled: !viewport.dragging && !viewport.flicking; NumberAnimation { duration: host.motion * 0.65; easing.type: Easing.OutCubic } }
        Row {
            id: tabs
            spacing: 24
            Repeater {
                id: buttons
                onItemAdded: Qt.callLater(navigation.updateIndicator)
                model: [qsTr("Треки"), qsTr("Плейлисты"), qsTr("Избранное"), qsTr("Репосты")]
                AbstractButton {
                    id: tab
                    required property int index
                    required property string modelData
                    objectName: "profileSection" + index
                    width: label.implicitWidth + (index > 1 ? 16 : 0)
                    height: 44
                    Accessible.name: modelData + (index > 1 ? qsTr(": выбрать вид коллекции") : "")
                    onClicked: {
                        if (index === 2) likesMenu.popup(tab, 0, height)
                        else if (index === 3) repostsMenu.popup(tab, 0, height)
                        else navigation.navigate(index, "")
                    }
                    contentItem: Item {
                        Text {
                            id: label
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.horizontalCenterOffset: tab.index > 1 ? -5 : 0
                            text: tab.modelData; textFormat: Text.PlainText
                            font.pixelSize: 13; font.weight: Font.Medium
                            color: navigation.section === tab.index ? host.accent : host.muted
                            Behavior on color { ColorAnimation { duration: host.motion / 2 } }
                        }
                        Text {
                            visible: tab.index > 1
                            anchors.left: label.right; anchors.leftMargin: 4; anchors.verticalCenter: label.verticalCenter
                            text: "⌄"; font.pixelSize: 13; color: label.color
                        }
                    }
                    background: Rectangle { radius: 6; color: host.ink; opacity: tab.down ? 0.06 : tab.hovered ? 0.03 : 0 }
                }
            }
        }
        Rectangle {
            objectName: "profileSectionIndicator"
            x: navigation.indicatorX
            width: navigation.indicatorWidth
            anchors.bottom: parent.bottom; height: 2; radius: 1; color: host.accent
            Behavior on x { NumberAnimation { duration: host.motion * 0.65; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: host.motion * 0.65; easing.type: Easing.OutCubic } }
        }
    }
    component Choice: MenuItem {
        implicitHeight: navigation.mobile ? 48 : 36
        checkable: true; autoExclusive: true
        contentItem: Text { text: parent.text; textFormat: Text.PlainText; font.pixelSize: 13; color: parent.checked ? host.accent : host.ink; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 10 }
        indicator: Item {}
        background: Rectangle { radius: 6; color: host.ink; opacity: parent.highlighted ? 0.06 : 0 }
    }
    Menu {
        id: likesMenu
        objectName: "profileLikesMenu"
        width: 210; padding: 4
        background: Rectangle { radius: 10; color: host.panel; border.color: host.raised }
        Choice { text: qsTr("Любимые треки"); checked: navigation.likes === "likes"; onTriggered: navigation.navigate(2, "likes") }
        Choice { text: qsTr("Любимые плейлисты"); checked: navigation.likes === "likedPlaylists"; onTriggered: navigation.navigate(2, "likedPlaylists") }
        Choice { text: qsTr("Любимые станции"); checked: navigation.likes === "likedStations"; onTriggered: navigation.navigate(2, "likedStations") }
    }
    Menu {
        id: repostsMenu
        objectName: "profileRepostsMenu"
        width: 210; padding: 4
        background: Rectangle { radius: 10; color: host.panel; border.color: host.raised }
        Choice { text: qsTr("Репосты треков"); checked: navigation.reposts === "reposts"; onTriggered: navigation.navigate(3, "reposts") }
        Choice { text: qsTr("Репосты плейлистов"); checked: navigation.reposts === "repostedPlaylists"; onTriggered: navigation.navigate(3, "repostedPlaylists") }
    }
}
