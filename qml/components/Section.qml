import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Pane {
    id: section
    property string heading: ""
    default property alias contents: column.data
    padding: 20
    background: Rectangle { color: section.palette.window; radius: 20 }
    contentItem: Column {
        id: column
        spacing: 16
        Text { width: parent.width; text: section.heading; color: section.palette.windowText; font.pixelSize: 20; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
    }
}
