import QtQuick

QtObject {
    id: selection
    property var rows: []
    property string context: ""
    property bool active: false
    property var selected: ({})
    property int anchor: -1
    property bool busy: false
    property string pending: ""
    property int revision: 0
    property string message: ""
    readonly property int count: Object.keys(selected).length
    property var identities: []
    onContextChanged: reset()
    onRowsChanged: {
        const next = rows.map(row => row.urn || "")
        if (identities.some((id, i) => next[i] !== id)) reset()
        identities = next
    }
    function reset() {
        revision++; pending = ""; busy = false; active = false
        selected = ({}); anchor = -1; message = ""
    }
    function eligible(index) { return index >= 0 && index < rows.length && (rows[index].urn || "").startsWith("soundcloud:tracks:") }
    function contains(index) { return !!selected[index] }
    function choose(index, modifiers = 0) {
        if (busy || !eligible(index)) return
        active = true
        const next = Object.assign({}, selected)
        if ((modifiers & Qt.ShiftModifier) && anchor >= 0) {
            for (let i = Math.min(anchor, index); i <= Math.max(anchor, index); ++i) if (eligible(i)) next[i] = true
        } else {
            if (next[index]) delete next[index]; else next[index] = true
            anchor = index
        }
        selected = next; message = ""
    }
    function allLoaded() {
        if (busy) return
        const next = ({})
        for (let i = 0; i < rows.length; ++i) if (eligible(i)) next[i] = true
        selected = next; active = true
    }
    function tracks() { return rows.filter((row, index) => !!selected[index]) }
    function begin(token) { busy = true; pending = token; message = "" }
    function finish(token, success, error) {
        if (!pending || token !== pending) return
        busy = false; pending = ""
        if (success) { reset(); message = "Добавлено" }
        else message = error
    }
}
