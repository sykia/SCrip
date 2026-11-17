import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: comments
    required property var host
    required property var preferences
    property bool mobile: false
    signal profileRequested(var user)
    signal composeRequested(string mention)
    signal panelMoveRequested(real dx, real dy)
    component Txt: Text { color: comments.host.ink; font.pixelSize: 13; textFormat: Text.PlainText; elide: Text.ElideRight }
    component CommentIcon: Canvas {
        property string name: ""
        property color ink: comments.host.ink
        onNameChanged: requestPaint()
        onInkChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset(); c.scale(width / 24, height / 24)
            c.strokeStyle = ink; c.lineWidth = 1.6; c.lineCap = "round"; c.lineJoin = "round"; c.beginPath()
            if (name === "back") { c.translate(24, 0); c.scale(-1, 1) }
            if (name === "arrow" || name === "back") { c.moveTo(5,12); c.lineTo(19,12); c.moveTo(14,7); c.lineTo(19,12); c.lineTo(14,17) }
            else if (name === "close") { c.moveTo(6,6); c.lineTo(18,18); c.moveTo(18,6); c.lineTo(6,18) }
            else if (name === "repeat") { c.arc(12,12,7,0.5,5.6); c.moveTo(20,5); c.lineTo(19,11); c.lineTo(14,8) }
            else { for (let y = 6; y <= 18; y += 6) { c.moveTo(5,y); c.lineTo(19,y) } }
            c.stroke()
        }
    }
    component Avatar: Artwork { radius: Math.min(width, height) / 2 }
    component Tool: AbstractButton {
        id: button
        property string symbol: ""
        property bool active: false
        hoverEnabled: true
        implicitWidth: symbol ? (comments.mobile ? 40 : 28) : Math.max(64, label.implicitWidth + 20)
        implicitHeight: comments.mobile ? 40 : 28
        Accessible.name: text
        opacity: enabled ? 1 : 0.4
        background: Rectangle {
            radius: 8
            color: button.active ? Qt.alpha(comments.host.ink, 0.07) : button.down || button.hovered ? Qt.alpha(comments.host.ink, 0.04) : "transparent"
            Behavior on color { ColorAnimation { duration: comments.host.motion / 2 } }
        }
        contentItem: Item {
            implicitWidth: label.implicitWidth
            CommentIcon { visible: !!button.symbol; anchors.centerIn: parent; width: 17; height: 17; name: button.symbol; ink: comments.host.ink }
            Txt { id: label; visible: !button.symbol; anchors.centerIn: parent; width: Math.min(implicitWidth, button.availableWidth); text: button.text; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
        }
    }

    property var track: ({})
    property bool compact: false
    readonly property bool narrow: !mobile && width < 250
    property bool movable: false
    readonly property bool automatic: preferences.timedComments
    property int seenCount: 0
    property bool following: true
    property bool timedPost: true
    readonly property int postTime: {
        if (!timeInput.text.trim()) return currentTrack ? sc.position : 0
        const match = /^(\d{1,3}):([0-5]\d)$/.exec(timeInput.text.trim())
        return match ? (Number(match[1]) * 60 + Number(match[2])) * 1000 : -1
    }
    readonly property bool validPostTime: !timedPost || (postTime >= 0 && (!track.duration || postTime <= track.duration))
    property string draftTrack: ""
    readonly property var dataState: sc.commentsState || ({})
    readonly property bool matches: dataState.track === track.urn
    readonly property bool currentTrack: !!track.urn && track.urn === host.api.current.urn
    readonly property var entries: {
        const items = matches && !hiddenComments ? (dataState.items || []) : []
        if (preferences.timedComments) return items.filter(c => c.timestamp >= 0).slice().sort((a, b) => a.timestamp - b.timestamp)
        return items.map((entry, order) => ({entry, order})).sort((a, b) => String(b.entry.created_at || "").localeCompare(String(a.entry.created_at || "")) || a.order - b.order).map(item => item.entry)
    }
    readonly property int timelineIndex: {
        if (!preferences.timedComments || !currentTrack) return -1
        let low = 0, high = entries.length
        while (low < high) { const mid = Math.floor((low + high) / 2); if (entries[mid].timestamp <= sc.position) low = mid + 1; else high = mid }
        return low - 1
    }
    readonly property int activeIndex: timelineIndex >= 0 && sc.position - entries[timelineIndex].timestamp < 8000 ? timelineIndex : -1
    onTimelineIndexChanged: centerActive(true)
    onVisibleChanged: if (visible) { sc.setCommentTrack(track); sc.loadComments(String(track.urn || "")) }
    readonly property bool canPost: track.commentable !== false
    readonly property bool hiddenComments: track.reveal_comments === false
    onTrackChanged: {
        if (visible) { sc.setCommentTrack(track); sc.loadComments(String(track.urn || "")) }
        if (draftTrack !== String(track.urn || "")) { draftTrack = String(track.urn || ""); commentInput.text = ""; timeInput.text = ""; following = true; seenCount = 0 }
    }
    padding: mobile ? 0 : compact ? 10 : 14
    background: Rectangle { radius: comments.mobile ? 0 : 14; color: comments.mobile ? "transparent" : host.panel; border.color: comments.compact ? (host.dark ? "#383838" : "#e6e2e5") : "transparent" }
    function centerActive(animate) {
        if (!following || !automatic || !currentTrack || !entries.length || !visible) return
        commentScroll.stop()
        const old = commentList.contentY
        commentList.positionViewAtIndex(Math.max(0, timelineIndex), ListView.Center)
        const next = commentList.contentY
        if (animate && host.motion > 0) { commentList.contentY = old; commentScroll.from = old; commentScroll.to = next; commentScroll.start() }
    }
    function setDraft(text) { if (text) commentInput.text = text; commentInput.forceActiveFocus() }
    function setReply(user) { commentInput.text = "@" + (user?.permalink || user?.username || "") + " "; commentInput.forceActiveFocus() }
    function focusComposer() { commentInput.forceActiveFocus() }
    function playAt(time) {
        if (!currentTrack) sc.play(track)
        sc.seek(time); sc.resumePlayback()
    }
    onActiveIndexChanged: centerActive(true)
    onEntriesChanged: {
        if (!automatic && seenCount === 0) Qt.callLater(function() { commentList.positionViewAtBeginning() })
        seenCount = entries.length
        Qt.callLater(function() { comments.centerActive(false) })
    }
    onAutomaticChanged: Qt.callLater(function() {
        comments.following = true
        if (comments.automatic) comments.centerActive(true)
        else { commentScroll.stop(); commentList.positionViewAtBeginning() }
    })
    Connections {
        target: sc
        function onCommentPosted(track) { if (track === comments.track.urn) { commentInput.text = ""; timeInput.text = "" } }
    }
    Timer {
        interval: 350; repeat: true
        running: comments.visible && comments.matches && preferences.timedComments && !!comments.dataState.hasMore && !comments.dataState.busy && !comments.dataState.error && (comments.dataState.items || []).length < 5000
        onTriggered: sc.loadComments(comments.track.urn, true)
    }
    contentItem: ColumnLayout {
        spacing: 6
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Item {
                Layout.fillWidth: true
                implicitHeight: titleLabel.implicitHeight
                Txt { id: titleLabel; anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: "Комментарии"; font.pixelSize: comments.narrow ? 12 : comments.mobile ? 18 : 15; font.weight: Font.DemiBold }
                MouseArea {
                    anchors.fill: parent
                    enabled: comments.movable
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property point lastPosition
                    onPressed: function(mouse) { lastPosition = mapToItem(comments.parent, mouse.x, mouse.y) }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const position = mapToItem(comments.parent, mouse.x, mouse.y)
                        comments.panelMoveRequested(position.x - lastPosition.x, position.y - lastPosition.y)
                        lastPosition = position
                    }
                }
            }
            Txt { visible: !comments.narrow; text: comments.matches ? (comments.dataState.items || []).length : ""; color: host.muted; font.pixelSize: 11 }
            Tool { visible: comments.movable; implicitWidth: comments.narrow ? 24 : 32; symbol: preferences.commentsLeft ? "arrow" : "back"; text: preferences.commentsLeft ? "Переместить вправо" : "Переместить влево"; onClicked: preferences.commentsLeft = !preferences.commentsLeft }
            Tool { visible: comments.movable; implicitWidth: comments.narrow ? 24 : 32; symbol: "close"; text: "Скрыть комментарии"; onClicked: preferences.showComments = false }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Tool { implicitHeight: comments.mobile ? 36 : 28; text: "Все"; implicitWidth: comments.narrow ? 42 : 56; active: !preferences.timedComments; onClicked: { preferences.timedComments = false; comments.following = true } }
            Tool { implicitHeight: comments.mobile ? 36 : 28; text: comments.narrow ? "Таймкоды" : "По времени"; implicitWidth: comments.narrow ? 88 : 110; active: preferences.timedComments; onClicked: { preferences.timedComments = true; comments.following = true; Qt.callLater(function() { comments.centerActive(true) }) } }
            Item { Layout.fillWidth: true }
            Tool { implicitHeight: comments.mobile ? 36 : 28; visible: !comments.narrow; symbol: "repeat"; text: "Обновить комментарии"; enabled: !comments.dataState.busy && !comments.dataState.sending; onClicked: sc.loadComments(comments.track.urn, false, true) }
        }
        RowLayout {
            visible: preferences.timedComments
            Layout.fillWidth: true
            Txt { Layout.fillWidth: true; text: comments.currentTrack ? "Сейчас · " + host.time(sc.position) : "Таймкоды этого трека"; font.pixelSize: 10; color: host.muted }
            Tool { visible: !comments.following; text: "Следить"; implicitWidth: 78; onClicked: { comments.following = true; comments.centerActive(true) } }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: comments.narrow ? 40 : 100
            ListView {
                id: commentList
                objectName: comments.compact ? "floatingCommentsList" : "trackCommentsList"
                anchors.fill: parent; clip: true; spacing: 2
                model: comments.entries
                boundsBehavior: Flickable.StopAtBounds
                topMargin: preferences.timedComments ? height / 2 - 36 : 0
                bottomMargin: preferences.timedComments ? height / 2 - 36 : 0
                onMovementStarted: { comments.following = false; commentScroll.stop() }
                ScrollBar.vertical: ScrollBar {}
                NumberAnimation { id: commentScroll; target: commentList; property: "contentY"; duration: host.motion; easing.type: Easing.InOutCubic }
                delegate: Rectangle {
                    id: commentRow
                    required property var modelData
                    required property int index
                    readonly property bool highlighted: comments.activeIndex === index
                    width: commentList.width; height: commentBody.implicitHeight + 14
                    radius: 8
                    color: highlighted ? Qt.alpha(host.accent, 0.07) : hover.hovered ? Qt.alpha(host.ink, 0.035) : "transparent"
                    opacity: preferences.timedComments && comments.activeIndex >= 0 && !highlighted && !hover.hovered ? 0.48 : 1
                    Behavior on color { ColorAnimation { duration: host.motion } }
                    Behavior on opacity { NumberAnimation { duration: host.motion } }
                    HoverHandler { id: hover }
                    ColumnLayout {
                        id: commentBody
                        x: 8; y: 7; width: parent.width - 16; spacing: 3
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Avatar { visible: !comments.narrow; width: 20; height: 20; source: commentRow.modelData.user?.avatar_url || "" }
                            AbstractButton {
                                Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: 26
                                Accessible.name: "Открыть профиль " + (commentRow.modelData.user?.username || "")
                                contentItem: Txt { text: commentRow.modelData.user?.username || "Пользователь SoundCloud"; font.pixelSize: 11; font.weight: Font.Medium; color: host.muted }
                                onClicked: { if (commentRow.modelData.user?.urn) { comments.profileRequested(commentRow.modelData.user) } }
                            }
                            Tool {
                                visible: commentRow.modelData.timestamp >= 0
                                implicitWidth: 44; implicitHeight: comments.mobile ? 36 : 24
                                text: host.time(commentRow.modelData.timestamp)
                                onClicked: comments.playAt(commentRow.modelData.timestamp)
                            }
                            Tool {
                                id: commentActions
                                text: "⋯"; implicitWidth: 28; implicitHeight: comments.mobile ? 36 : 24
                                Accessible.name: "Действия с комментарием"
                                opacity: comments.mobile || hover.hovered || hovered || visualFocus || actionsMenu.opened ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: host.motion / 2 } }
                                onClicked: actionsMenu.open()
                                Menu {
                                    id: actionsMenu
                                    MenuItem {
                                        text: "Ответить"; enabled: comments.canPost && sc.signedIn
                                        onTriggered: {
                                            const mention = "@" + (commentRow.modelData.user?.permalink || commentRow.modelData.user?.username || "") + " "
                                            if (comments.narrow) comments.composeRequested(mention)
                                            else comments.setDraft(mention)
                                        }
                                    }
                                    MenuItem { text: "Копировать текст"; onTriggered: sc.copyText(commentRow.modelData.body || "") }
                                }
                            }
                        }
                        Txt {
                            Layout.fillWidth: true; text: commentRow.modelData.body || ""
                            textFormat: Text.PlainText; wrapMode: Text.Wrap; elide: Text.ElideNone
                            font.pixelSize: comments.compact ? 12 : 13; lineHeight: 1.2
                            color: commentRow.highlighted ? host.accent : host.ink
                            Behavior on color { ColorAnimation { duration: host.motion } }
                        }

                    }
                }
                footer: Tool {
                    width: commentList.width
                    visible: comments.matches && !!comments.dataState.hasMore
                    height: visible ? 34 : 0
                    text: comments.dataState.busy ? "Загружаем…" : "Ещё комментарии"
                    enabled: !comments.dataState.busy
                    onClicked: sc.loadComments(comments.track.urn, true)
                }
            }
            Column {
                anchors.centerIn: parent; width: parent.width - 12; spacing: 10
                visible: comments.entries.length === 0
                Txt {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: host.muted; font.pixelSize: 12
                    text: comments.hiddenComments ? "Комментарии скрыты автором" : comments.dataState.busy ? "Загружаем комментарии…" : comments.dataState.error ? comments.dataState.error : preferences.timedComments ? "Здесь появятся комментарии\nс отметками времени" : (comments.canPost ? "Пока нет комментариев.\nМожно оставить первый." : "Пока нет комментариев")
                }
                Tool { anchors.horizontalCenter: parent.horizontalCenter; visible: !!comments.dataState.error; text: "Повторить"; onClicked: sc.loadComments(comments.track.urn, false, true) }
            }
        }
        Txt { Layout.fillWidth: true; visible: comments.entries.length > 0 && !!comments.dataState.error; text: comments.dataState.error || ""; color: host.muted; wrapMode: Text.Wrap; font.pixelSize: 11 }
        Rectangle { Layout.fillWidth: true; height: 1; color: host.dark ? "#383838" : "#e6e2e5" }
        RowLayout {
            visible: !comments.narrow
            Layout.fillWidth: true; spacing: 6
            ScrollView {
                id: composerScroll
                implicitWidth: 0; Layout.minimumWidth: 0; Layout.preferredWidth: 1
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(84, Math.max(comments.mobile ? 40 : 34, commentInput.implicitHeight))
                contentWidth: availableWidth; clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                TextArea {
                    id: commentInput
                    implicitWidth: 160
                    implicitHeight: Math.max(34, contentHeight + topPadding + bottomPadding)
                    objectName: comments.compact ? "floatingCommentInput" : "trackCommentInput"
                    width: composerScroll.availableWidth
                    enabled: comments.canPost && sc.signedIn && !comments.dataState.sending
                    placeholderText: !comments.canPost ? "Автор отключил комментирование" : sc.signedIn ? "Написать комментарий…" : "Войдите, чтобы написать"
                    color: host.ink; placeholderTextColor: host.muted; font.pixelSize: 12
                    wrapMode: TextEdit.Wrap; selectByMouse: true
                    leftPadding: 10; rightPadding: 10
                    topPadding: 9; bottomPadding: 9
                    verticalAlignment: TextEdit.AlignVCenter
                    background: Rectangle { radius: 10; color: host.surface; border.color: commentInput.activeFocus ? Qt.alpha(host.accent, 0.5) : "transparent" }
                }
            }
            Tool { Layout.alignment: Qt.AlignBottom; objectName: "postCommentButton"; symbol: "arrow"; text: comments.dataState.sending ? "Отправляем…" : "Отправить комментарий"; enabled: comments.canPost && sc.signedIn && comments.matches && !comments.dataState.sending && commentInput.text.trim().length > 0 && commentInput.text.length <= 1000 && comments.validPostTime; onClicked: sc.postComment(comments.track.urn, commentInput.text, comments.timedPost ? comments.postTime : -1) }
        }
        Tool {
            visible: comments.narrow; enabled: comments.canPost && sc.signedIn; Layout.fillWidth: true; text: "Написать"
            onClicked: { comments.composeRequested("") }
        }
        RowLayout {
            visible: !comments.narrow
            Layout.fillWidth: true; spacing: 4
            Tool {
                text: "Таймкод"; active: comments.timedPost; implicitWidth: 74; implicitHeight: 28
                Accessible.checkable: true; Accessible.checked: comments.timedPost
                onClicked: comments.timedPost = !comments.timedPost
            }
            TextField {
                id: timeInput
                objectName: "commentTimestampInput"
                visible: comments.timedPost
                Layout.preferredWidth: 58; implicitHeight: 28
                color: host.ink; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter
                placeholderText: host.time(comments.currentTrack ? sc.position : 0)
                placeholderTextColor: host.muted
                Accessible.name: "Таймкод комментария, минуты и секунды"
                selectByMouse: true; inputMethodHints: Qt.ImhNoPredictiveText
                maximumLength: 6
                background: Rectangle { radius: 8; color: host.surface; border.color: !comments.validPostTime ? host.accent : timeInput.activeFocus ? Qt.alpha(host.accent, 0.5) : "transparent" }
            }
            Item { Layout.fillWidth: true }

        }
        Txt { Layout.fillWidth: true; visible: !!comments.dataState.postError || commentInput.text.length > 1000; text: commentInput.text.length > 1000 ? "Не больше 1000 символов" : comments.dataState.postError || ""; color: host.muted; wrapMode: Text.Wrap; font.pixelSize: 11 }
    }
}
