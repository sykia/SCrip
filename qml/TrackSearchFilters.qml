import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Column {
    id: root
    required property var host
    required property var backend
    property bool mobile: false
    property bool expanded: false
    property var applied: ({})
    property string error: ""
    signal searchRequested()
    spacing: 8
    function draft() {
        let result = {}
        for (let i = 0; i < groups.count; ++i) {
            const inputs = groups.itemAt(i).inputs
            for (let j = 0; j < inputs.count; ++j) result[inputs.itemAt(j).modelData.key] = inputs.itemAt(j).text
        }
        result.fullOnly = fullOnly.checked
        return result
    }
    function apply() {
        const values = draft()
        const validation = backend.validateTrackFilters(values)
        error = validation.error || ""
        if (error || !backend.applyTrackFilters(values)) return false
        applied = values
        searchRequested()
        return true
    }
    function restore(values) {
        for (let i = 0; i < groups.count; ++i) {
            const inputs = groups.itemAt(i).inputs
            for (let j = 0; j < inputs.count; ++j) inputs.itemAt(j).text = values[inputs.itemAt(j).modelData.key] || ""
        }
        fullOnly.checked = !!values.fullOnly
        return apply()
    }
    function reset() {
        for (let i = 0; i < groups.count; ++i) {
            const inputs = groups.itemAt(i).inputs
            for (let j = 0; j < inputs.count; ++j) inputs.itemAt(j).text = ""
        }
        fullOnly.checked = false
        apply()
    }
    readonly property string summary: {
        let parts = []
        if (applied.genres) parts.push(applied.genres)
        if (applied.tags) parts.push(qsTr("Теги: ") + applied.tags)
        const range = (from, to, suffix) => {
            if (applied[from] || applied[to]) parts.push((applied[from] || "…") + "–" + (applied[to] || "…") + suffix)
        }
        range("durationFrom", "durationTo", qsTr(" с"))
        range("bpmFrom", "bpmTo", " BPM")
        range("createdFrom", "createdTo", "")
        if (applied.fullOnly) parts.push(qsTr("Полные треки"))
        return parts.join(" · ")
    }
    readonly property int activeCount: [applied.genres, applied.tags, applied.durationFrom || applied.durationTo, applied.bpmFrom || applied.bpmTo, applied.createdFrom || applied.createdTo, applied.fullOnly].filter(value => !!value).length
    RowLayout {
        width: parent.width; spacing: 8
        Button {
            id: toggle
            Layout.preferredHeight: root.mobile ? 44 : 34
            implicitWidth: toggleLabel.implicitWidth + 4
            padding: 0; hoverEnabled: true
            Accessible.name: qsTr("Фильтры поиска")
            Accessible.description: root.expanded ? qsTr("Развернуто") : qsTr("Свернуто")
            contentItem: Row {
                id: toggleLabel
                spacing: 8
                Text { text: qsTr("Фильтры"); color: toggle.hovered || root.expanded ? root.host.ink : root.host.muted; font.pixelSize: 13; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
                Text { visible: root.activeCount > 0; text: root.activeCount; color: root.host.accent; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                Text { text: root.expanded ? "⌃" : "⌄"; color: root.host.muted; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
            }
            background: Rectangle { color: "transparent"; radius: 6; border.color: toggle.activeFocus ? root.host.accent : "transparent" }
            onClicked: root.expanded = !root.expanded
        }
        Item { Layout.fillWidth: true }
        SurfaceButton { host: root.host; mobile: root.mobile; quiet: true; visible: root.expanded || !!root.summary; text: qsTr("Сбросить"); onClicked: root.reset() }
    }
    Text { width: parent.width; visible: !!text; text: root.summary; color: root.host.muted; wrapMode: Text.Wrap; font.pixelSize: 12; textFormat: Text.PlainText }
    Column {
        width: parent.width; spacing: 12; visible: root.expanded
        GridLayout {
            id: grid
            width: parent.width; columns: width >= 700 ? 3 : 2
            columnSpacing: 12; rowSpacing: 12
            Repeater {
                id: groups
                model: [
                    {label: qsTr("Жанр"), fields: [{key: "genres", hint: "House, Pop"}]},
                    {label: qsTr("Теги"), fields: [{key: "tags", hint: "chill, piano"}]},
                    {label: qsTr("Длительность · с"), fields: [{key: "durationFrom", hint: qsTr("От")}, {key: "durationTo", hint: qsTr("До")}]},
                    {label: qsTr("Темп · BPM"), fields: [{key: "bpmFrom", hint: qsTr("От")}, {key: "bpmTo", hint: qsTr("До")}]},
                    {label: qsTr("Дата загрузки · UTC"), date: true, fields: [{key: "createdFrom", hint: qsTr("С: ГГГГ-ММ-ДД")}, {key: "createdTo", hint: qsTr("По: ГГГГ-ММ-ДД")}]}
                ]
                ColumnLayout {
                    required property var modelData
                    property alias inputs: inputs
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Layout.columnSpan: modelData.date ? 2 : 1
                    Layout.preferredWidth: ((grid.width - (grid.columns - 1) * grid.columnSpacing) / grid.columns) * Layout.columnSpan + (Layout.columnSpan - 1) * grid.columnSpacing
                    spacing: 6
                    Text { Layout.fillWidth: true; text: modelData.label; color: root.host.muted; font.pixelSize: 11; elide: Text.ElideRight }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: root.mobile ? 44 : 36
                        radius: 8
                        color: Qt.alpha(root.host.ink, root.host.dark ? 0.045 : 0.035)
                        border.color: Qt.alpha(root.host.ink, 0.10)
                        RowLayout {
                            id: inputRow
                            anchors.fill: parent; spacing: 0
                            Repeater {
                                id: inputs
                                model: modelData.fields
                                TextField {
                                    required property var modelData
                                    objectName: modelData.key
                                    Layout.fillWidth: true; Layout.minimumWidth: 0
                                    implicitWidth: 50; implicitHeight: root.mobile ? 44 : 36
                                    leftPadding: 10; rightPadding: 8
                                    placeholderText: modelData.hint
                                    Accessible.name: ({genres: qsTr("Жанры через запятую"), tags: qsTr("Теги через запятую"), durationFrom: qsTr("Длительность от, секунды"), durationTo: qsTr("Длительность до, секунды"), bpmFrom: qsTr("BPM от"), bpmTo: qsTr("BPM до"), createdFrom: qsTr("Загружено с, ГГГГ-ММ-ДД"), createdTo: qsTr("Загружено по, ГГГГ-ММ-ДД")})[modelData.key]
                                    color: root.host.ink; placeholderTextColor: Qt.alpha(root.host.muted, 0.7)
                                    selectByMouse: true; font.pixelSize: 12
                                    inputMethodHints: modelData.key.startsWith("duration") || modelData.key.startsWith("bpm") ? Qt.ImhDigitsOnly : Qt.ImhNone
                                    background: Rectangle {
                                        color: "transparent"; radius: 8
                                        border.color: parent.activeFocus ? Qt.alpha(root.host.accent, 0.7) : "transparent"
                                        Rectangle { visible: modelData.key.endsWith("To"); width: 1; height: 16; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; color: Qt.alpha(root.host.ink, 0.12) }
                                    }
                                    onAccepted: root.apply()
                                }
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            width: parent.width; spacing: 8
            CheckBox {
                id: fullOnly
                Layout.fillWidth: true; Layout.minimumWidth: 0
                implicitHeight: root.mobile ? 44 : 36
                spacing: 8; padding: 0
                text: qsTr("Только полные треки")
                indicator: Rectangle {
                    x: 0; y: (fullOnly.height - height) / 2
                    width: 16; height: 16; radius: 4
                    color: fullOnly.checked ? root.host.accent : "transparent"
                    border.color: fullOnly.activeFocus ? root.host.accent : Qt.alpha(root.host.ink, 0.3)
                    Text { anchors.centerIn: parent; visible: fullOnly.checked; text: "✓"; color: root.host.foreground(root.host.accent); font.pixelSize: 12 }
                }
                contentItem: Text { text: fullOnly.text; leftPadding: 24; color: root.host.ink; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; wrapMode: Text.Wrap }
                onClicked: root.error = ""
            }
            SurfaceButton { host: root.host; mobile: root.mobile; text: qsTr("Применить"); primary: true; onClicked: root.apply() }
        }
        Text { width: parent.width; text: qsTr("Жанр и BPM — по метаданным SoundCloud"); color: Qt.alpha(root.host.muted, 0.8); font.pixelSize: 11; wrapMode: Text.Wrap }
        Text { width: parent.width; visible: !!root.error; text: root.error; color: root.host.dark ? "#ff8c8c" : "#bb3535"; wrapMode: Text.Wrap; font.pixelSize: 12 }
    }
}
