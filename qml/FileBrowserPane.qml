import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One pane of the dual-pane browse tab. Same component drives the local and remote
// sides; the parent supplies the model + path and reacts to navigation signals.
// Multi-select holds selected *names* (unique within a directory) so the parent can
// act on them without reaching into the C++ model from QML.
ColumnLayout {
    id: pane

    property string title
    property var fileModel
    property string path
    property bool busy: false
    property bool enabledActions: true
    property bool allowContext: false
    property var selected: []

    signal upRequested()
    signal refreshRequested()
    signal openDir(string name)
    signal contextMenuRequested()

    function clearSelection() { selected = [] }
    function isSelected(name) { return selected.indexOf(name) >= 0 }

    function toggle(name, additive) {
        if (additive) {
            var copy = selected.slice()
            var i = copy.indexOf(name)
            if (i >= 0) copy.splice(i, 1)
            else copy.push(name)
            selected = copy
        } else {
            selected = [name]
        }
    }

    spacing: 6

    // Header: title + path + up/refresh.
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Text { text: pane.title; color: Theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1 }
        Item { Layout.fillWidth: true }
        FlatButton { label: "Up"; active: pane.enabledActions && !pane.busy; onClicked: pane.upRequested() }
        FlatButton { label: "Refresh"; active: pane.enabledActions && !pane.busy; onClicked: pane.refreshRequested() }
    }

    Text {
        Layout.fillWidth: true
        text: pane.path
        color: Theme.textSecondary
        font.family: Theme.mono
        font.pixelSize: 11
        elide: Text.ElideMiddle
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: Theme.bgPrimary
        radius: Theme.radius
        border.width: 1
        border.color: Theme.border

        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            model: pane.fileModel
            ScrollBar.vertical: ScrollBar {}

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: pane.busy ? "Loading…" : "Empty"
                color: Theme.textTertiary
                font.pixelSize: 12
            }

            delegate: Rectangle {
                id: row
                required property string name
                required property bool isDir
                required property bool isSymlink
                required property string sizeText
                required property string mtimeText

                readonly property bool sel: pane.isSelected(name)
                width: ListView.view.width
                height: 26
                radius: Theme.radius
                color: sel ? Theme.bgTertiary
                           : (rowMouse.containsMouse ? Theme.bgSecondary : "transparent")
                border.width: sel ? 1 : 0
                border.color: Theme.accent

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    Text {
                        text: row.isDir ? "📁" : (row.isSymlink ? "🔗" : "📄")
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.name
                        color: Theme.textPrimary
                        font.family: Theme.mono
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                    Text {
                        text: row.sizeText
                        color: Theme.textTertiary
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 64
                    }
                    Text {
                        text: row.mtimeText
                        color: Theme.textTertiary
                        font.pixelSize: 11
                        Layout.preferredWidth: 96
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) {
                        var additive = (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)) !== 0
                        pane.toggle(row.name, additive)
                    }
                    onDoubleClicked: { if (row.isDir) pane.openDir(row.name) }
                }
            }
        }

        // Right-click anywhere in the list (rows or empty space) opens the parent's
        // context menu. Left-clicks aren't accepted here, so they fall through to the
        // row delegates for selection.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            enabled: pane.allowContext
            onClicked: pane.contextMenuRequested()
        }
    }
}
