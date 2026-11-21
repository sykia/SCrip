import QtQuick
import QtTest
import "../../qml" as Scrip

TestCase {
    name: "MetadataLabel"
    visible: true
    when: windowShown
    width: 400; height: 300
    Component {
        id: fixture
        Scrip.MetadataLabel {
            normalizeFallback: true
            width: 240; font.pixelSize: 16
            elide: Text.ElideRight
            text: "бамжыха иуу (prod kurane🥩❤️🙏🏻🤙🏻, kayoshi) — hinizaza (og shiba🐶)"
        }
    }
    function test_singleLineKeepsEllipsisAndBounds() {
        const label = createTemporaryObject(fixture, this)
        waitForRendering(label)
        verify(label.truncated)
        compare(label.lineCount, 1)
        verify(label.implicitHeight > 0 && label.implicitHeight <= 32)
        verify(label.contentWidth <= label.width + 1)
        verify(label.clip)
    }
    function test_multilineRemainsBounded() {
        const label = createTemporaryObject(fixture, this, {wrapMode: Text.Wrap, maximumLineCount: 3})
        waitForRendering(label)
        verify(label.lineCount > 1 && label.lineCount <= 3)
        verify(label.implicitHeight <= 96)
        label.width = 360
        waitForRendering(label)
        verify(label.implicitHeight <= 96)
    }
    function test_descriptionLineSpacing() {
        const label = createTemporaryObject(fixture, this, {
            wrapMode: Text.Wrap, maximumLineCount: 3, lineHeight: 1.4
        })
        waitForRendering(label)
        verify(label.lineCount > 1)
        verify(label.implicitHeight > 0 && label.implicitHeight < 120)
        compare(label.lineHeight, 1.4)
    }
    function test_metadataIsPlainText() {
        const label = createTemporaryObject(fixture, this, {text: "<b>artist 🐶</b>"})
        compare(label.textFormat, Text.PlainText)
        compare(label.text, "<b>artist 🐶</b>")
    }
}
