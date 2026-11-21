import QtQuick
import QtQuick.Controls.Basic
import SCrip.Native 1.0

// Shared by metadata surfaces: fallback emoji metrics must not enlarge a row.
Label {
    id: label
    property bool normalizeFallback: Qt.platform.os === "android"
    property FontMetrics normalMetrics: FontMetrics { font: label.font }
    textFormat: Text.PlainText
    clip: normalizeFallback
    onLineLaidOut: line => {
        // Custom line layout needs the actual viewport width for eliding too.
        if (wrapMode === Text.NoWrap && width > 0)
            line.width = Math.max(1, width - leftPadding - rightPadding)
        if (!normalizeFallback || line.height <= font.pixelSize * 2) return
        // Qt has already applied proportional leading to the oversized run.
        // Remove that offset before applying the primary font's line spacing.
        const extraLeading = lineHeightMode === Text.ProportionalHeight && lineHeight > 0
            ? line.height * (1 - 1 / lineHeight) : 0
        const top = line.y - extraLeading
        const ascent = TextGeometry.lineAscent(text, font, line.width, line.number, wrapMode !== Text.NoWrap)
        line.height = lineHeightMode === Text.FixedHeight ? lineHeight
            : Math.ceil(normalMetrics.height * lineHeight)
        line.y = top + normalMetrics.ascent - ascent
    }
}
