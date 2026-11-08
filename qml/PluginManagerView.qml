import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

Column {
    id: root
    property var manager: null
    property string message: ""
    property string operationId: ""
    property var preview: ({})
    property string removalId: ""
    property string appearanceHint: ""
    property color ink: "#242426"
    property color muted: "#707075"
    spacing: 12
    function permissionLabel(id) {
        return ({"ui.panels":"Панели и кнопки", "ui.theme":"Изменение оформления", "player.observe":"Названия и исполнители треков, состояние плеера (персональные данные)", "player.control":"Управление воспроизведением", "navigation":"Переходы между страницами", "storage":"Собственные данные", "timers":"Одноразовые таймеры"})[id] || id
    }
    function showResult(error) { message = error || "Изменения сохранены" }
    Dialogs.FileDialog {
        id: picker
        title: "Установить или обновить плагин"
        nameFilters: ["Плагины SCrip (*.json)"]
        onAccepted: {
            const result = root.manager.prepareInstall(selectedFile)
            if (result.error) { root.message = result.error; return }
            root.operationId = result.operationId
            root.preview = ({})
            consent.checked = false
            review.open()
        }
    }
    Connections {
        target: root.manager
        function onInstallPrepared(result) {
            if (result.operationId !== root.operationId) return
            if (result.error) { root.message = result.error; review.close(); return }
            root.preview = result
        }
    }
    Dialog {
        id: review
        objectName: "plugin-install-review"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 24)
        height: Math.min(660, parent.height - 32)
        modal: true
        palette.window: "#faf9fa"
        palette.windowText: root.ink
        palette.text: root.ink
        title: "Проверка плагина"
        closePolicy: Popup.CloseOnEscape
        onClosed: { if (root.operationId && root.manager) root.manager.cancelInstall(root.operationId); root.operationId = "" }
        contentItem: ScrollView {
            clip: true; contentWidth: availableWidth
            Column {
                width: parent.width; spacing: 12
                Label { width: parent.width; text: root.preview.plugin ? root.preview.plugin.name + " · " + (root.preview.plugin.version || "API v1") : "Читаем и проверяем файл…"; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.bold: true }
                Label { width: parent.width; text: root.preview.plugin ? root.preview.plugin.id + " · API " + root.preview.plugin.api : ""; textFormat: Text.PlainText; wrapMode: Text.Wrap }
                Label { width: parent.width; text: root.preview.plugin?.description || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text }
                Label { width: parent.width; text: root.preview.previousVersion ? "Предыдущая версия: " + root.preview.previousVersion : "Новый плагин будет установлен выключенным."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
                Label { width: parent.width; text: "Файл не подписан. Совпадение ID не подтверждает автора. Устанавливайте пакеты из источника, которому доверяете."; wrapMode: Text.Wrap }
                Label { width: parent.width; text: "SHA-256: " + (root.preview.sha256 || "…"); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; font.pixelSize: 11 }
                Repeater {
                    model: root.preview.permissions || []
                    Label { required property var modelData; width: parent.width; text: "• " + root.permissionLabel(modelData); textFormat: Text.PlainText; wrapMode: Text.Wrap }
                }
                Repeater {
                    model: root.preview.changes || []
                    Label { required property var modelData; width: parent.width; text: modelData; textFormat: Text.PlainText; wrapMode: Text.Wrap }
                }
                CheckBox {
                    id: consent
                    width: parent.width
                    enabled: !!root.preview.plugin
                    text: "Подтверждаю замену и перечисленные разрешения"
                    contentItem: Label { text: consent.text; color: root.ink; wrapMode: Text.Wrap; leftPadding: consent.indicator.width + consent.spacing; verticalAlignment: Text.AlignVCenter }
                }
                Flow {
                    width: parent.width; spacing: 8
                    PluginAction {
                        text: "Установить"
                        enabled: consent.checked && !!root.preview.plugin
                        onClicked: {
                            root.showResult(root.manager.commitInstall(root.operationId, root.preview.permissions || []))
                            root.operationId = ""
                            review.close()
                        }
                    }
                    PluginAction { width: Math.min(implicitWidth, parent.width); text: "Отмена"; onClicked: review.close() }
                }
            }
        }
    }
    Dialog {
        id: removal
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent.width - 24)
        modal: true
        palette.window: "#faf9fa"
        palette.windowText: root.ink
        palette.text: root.ink
        title: "Удаление плагина"
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Column {
            spacing: 12
            Label { width: parent.width; text: "Удалить «" + root.removalId + "»? Его зависимые плагины будут отключены.\n" + (root.manager ? root.manager.dependents(root.removalId).join(", ") : ""); textFormat: Text.PlainText; wrapMode: Text.Wrap }
            CheckBox { id: keep; text: "Сохранить настройки и данные" }
        }
        onAccepted: root.showResult(root.manager.remove(root.removalId, keep.checked))
    }
    Flow {
        width: parent.width; spacing: 8
        PluginAction { width: Math.min(implicitWidth, parent.width); text: "Установить / обновить из файла…"; enabled: !!root.manager && !root.manager.error; onClicked: picker.open() }
    }
    Label { width: parent.width; text: root.manager ? root.manager.error : "Менеджер недоступен"; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text; color: "#a22c39" }
    Label { width: parent.width; text: root.message; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text; color: root.ink }
    Label { width: parent.width; text: "API v1 и декларативный API v2. Исполняемые расширения не поддерживаются. Обновления устанавливаются из файла."; wrapMode: Text.Wrap; color: root.muted }
    Label { width: parent.width; text: root.appearanceHint; wrapMode: Text.Wrap; visible: !!text; color: root.muted }
    Repeater {
        model: root.manager ? root.manager.installed : []
        Rectangle {
            id: card
            required property var modelData
            objectName: "plugin-card-" + modelData.plugin.id
            width: root.width; height: body.implicitHeight + 24; radius: 14
            color: "#f5f2f5"; border.color: "#e6e0e6"
            Column {
                id: body
                x: 12; y: 12; width: parent.width - 24; spacing: 10
                Label { width: parent.width; text: card.modelData.plugin.name + " · " + card.modelData.version; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.bold: true; color: root.ink }
                Label { width: parent.width; text: card.modelData.plugin.id + " · API " + card.modelData.plugin.api; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.muted }
                Label { width: parent.width; text: card.modelData.plugin.description || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text; color: root.ink }
                Label { width: parent.width; text: ({"active":"Работает", "disabled":"Отключён", "removed":"Пакет удалён, конфигурация сохранена", "awaiting-permissions":"Нужны разрешения", "blocked":"Заблокирован", "error":"Ошибка"})[card.modelData.status] + (card.modelData.reason ? ": " + card.modelData.reason : ""); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.ink }
                Label { width: parent.width; text: card.modelData.lastError || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text; color: "#a22c39" }
                Label { width: parent.width; text: card.modelData.themeStatus || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: !!text; color: root.muted }
                Flow {
                    width: parent.width; spacing: 8
                    PluginAction { width: Math.min(implicitWidth, parent.width); visible: card.modelData.status !== "removed"; text: card.modelData.enabled ? "Отключить" : "Включить"; onClicked: root.showResult(root.manager.enable(card.modelData.plugin.id, !card.modelData.enabled)) }
                    PluginAction { width: Math.min(implicitWidth, parent.width); text: "Обновить из файла"; onClicked: picker.open() }
                    PluginAction { width: Math.min(implicitWidth, parent.width); text: "Удалить"; onClicked: { root.removalId = card.modelData.plugin.id; keep.checked = false; removal.open() } }
                    PluginAction { width: Math.min(implicitWidth, parent.width); text: "Очистить ошибку"; visible: !!card.modelData.lastError; onClicked: root.manager.clearError(card.modelData.plugin.id) }
                }
                Repeater {
                    model: card.modelData.permissionRows || []
                    CheckBox {
                        id: permission
                        required property var modelData
                        width: body.width
                        checked: modelData.granted
                        text: modelData.label
                        contentItem: Label { text: permission.text; color: root.ink; textFormat: Text.PlainText; wrapMode: Text.Wrap; leftPadding: permission.indicator.width + permission.spacing; verticalAlignment: Text.AlignVCenter }
                        onToggled: root.showResult(root.manager.setPermission(card.modelData.plugin.id, modelData.id, checked))
                    }
                }
                Repeater {
                    model: card.modelData.settingRows || []
                    Column {
                        id: setting
                        required property var modelData
                        width: body.width; spacing: 4
                        Label { width: parent.width; text: setting.modelData.label; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.ink }
                        Switch { palette.windowText: root.ink; visible: setting.modelData.type === "boolean"; checked: setting.modelData.value === true; text: "Включено"; onToggled: root.showResult(root.manager.setSetting(card.modelData.plugin.id, setting.modelData.id, checked)) }
                        SpinBox { palette.base: "#ffffff"; palette.text: root.ink; palette.button: "#ede7ee"; palette.buttonText: root.ink; visible: setting.modelData.type === "integer"; from: setting.modelData.min || 0; to: setting.modelData.max || 0; value: setting.modelData.type === "integer" ? setting.modelData.value : 0; editable: true; onValueModified: root.showResult(root.manager.setSetting(card.modelData.plugin.id, setting.modelData.id, value)) }
                        ComboBox { palette.base: "#ffffff"; palette.text: root.ink; palette.button: "#ede7ee"; palette.buttonText: root.ink; visible: setting.modelData.type === "enum"; width: Math.min(280, parent.width); model: setting.modelData.options || []; currentIndex: (setting.modelData.options || []).indexOf(setting.modelData.value); onActivated: root.showResult(root.manager.setSetting(card.modelData.plugin.id, setting.modelData.id, currentText)) }
                    }
                }
                Repeater {
                    model: card.modelData.plugin.groups || []
                    CheckBox {
                        required property var modelData
                        required property int index
                        width: body.width
                        text: modelData.label; checked: card.modelData.flags[index]
                        onToggled: root.showResult(root.manager.configure(card.modelData.plugin.id, index, checked))
                        contentItem: Label { text: parent.text; color: root.ink; textFormat: Text.PlainText; wrapMode: Text.Wrap; leftPadding: parent.indicator.width + parent.spacing }
                    }
                }
                Label { width: parent.width; text: "Собственные данные: " + JSON.stringify(card.modelData.data || {}); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; visible: card.modelData.plugin.api === 2 && Object.keys(card.modelData.data || {}).length > 0; color: root.muted; maximumLineCount: 6; elide: Text.ElideRight }
                PluginAction { width: Math.min(implicitWidth, parent.width); visible: card.modelData.plugin.api === 2; text: "Очистить данные"; onClicked: root.showResult(root.manager.clearData(card.modelData.plugin.id)) }
            }
        }
    }
    Label { text: "Пока нет установленных плагинов"; visible: !!root.manager && !root.manager.installed.length; color: root.muted }
}
