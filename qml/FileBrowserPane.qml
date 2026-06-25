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
    property string info: ""   // optional right-aligned header note (e.g. free space)
    property var fileModel
    property string path
    property bool busy: false
    property bool enabledActions: true
    property bool allowContext: false
    property string side: ""        // "local" or "remote" — identifies the drag origin
    property bool acceptFileDrops: false  // accept external (Finder) file drops
    property var selected: []

    property bool dropHighlight: false   // set while a compatible drag hovers this pane

    signal upRequested()
    signal refreshRequested()
    signal pathSubmitted(string path)
    signal openDir(string name)
    signal contextMenuRequested()
    // Drag of the current selection began/moved/ended; coords are scene-relative.
    signal dragBegan(string side, var names, real sceneX, real sceneY)
    signal dragMoved(real sceneX, real sceneY)
    signal dragEnded()
    // A drag from the other pane was dropped here.
    signal itemsDropped(string fromSide, var names)
    // External files (Finder) were dropped here; urls is a list of file URLs.
    signal filesDropped(var urls)

    function clearSelection() { selected = [] }
    function isSelected(name) { return selected.indexOf(name) >= 0 }

    // Clear the name filter when the directory changes, so a stale filter doesn't hide
    // everything in the folder you just opened.
    onPathChanged: { filterField.text = ""; if (fileModel) fileModel.filter = "" }

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
        // fillWidth + elide so a long "REMOTE · user@host" title shrinks instead of
        // forcing the pane (and the whole window) wider.
        Text {
            text: pane.title
            color: Theme.textTertiary
            font.pixelSize: 11
            font.letterSpacing: 1
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Text {
            visible: pane.info.length > 0
            text: pane.info
            color: Theme.textTertiary
            font.pixelSize: 11
            elide: Text.ElideLeft
            Layout.maximumWidth: 160
        }
        Field {
            id: filterField
            Layout.preferredWidth: 120
            implicitHeight: 24
            placeholderText: "filter"
            enabled: pane.enabledActions && !pane.busy
            font.pixelSize: 12
            onTextChanged: if (pane.fileModel) pane.fileModel.filter = text
        }
        FlatButton { label: "Up"; active: pane.enabledActions && !pane.busy; onClicked: pane.upRequested() }
        FlatButton { label: "Refresh"; active: pane.enabledActions && !pane.busy; onClicked: pane.refreshRequested() }
    }

    Field {
        id: pathField
        Layout.fillWidth: true
        text: pane.path
        enabled: pane.enabledActions && !pane.busy
        selectByMouse: true
        onAccepted: pane.pathSubmitted(text)
        onActiveFocusChanged: if (!activeFocus) text = pane.path
    }

    // Breadcrumb: each path segment is clickable to jump straight to that ancestor.
    // Handles both POSIX ("/home/user") and drive-rooted ("C:/Users") paths.
    readonly property bool pathIsPosix: ("" + pane.path).charAt(0) === "/"
    function crumbParts(p) {
        p = ("" + p).replace(/\\/g, "/")
        var parts = p.split("/").filter(function(s) { return s.length > 0 })
        var posix = p.charAt(0) === "/"
        var out = []
        var acc = ""
        for (var i = 0; i < parts.length; i++) {
            acc += (posix || i > 0 ? "/" : "") + parts[i]
            out.push({ label: parts[i], path: acc + "/" })
        }
        return out
    }

    Flow {
        Layout.fillWidth: true
        spacing: 3
        visible: pane.path.length > 0
        // Root crumb (POSIX), and the only crumb when the path is just "/".
        Text {
            visible: pane.pathIsPosix
            text: "/"
            color: Theme.textTertiary
            font.pixelSize: 11
            MouseArea {
                anchors.fill: parent
                enabled: pane.enabledActions && !pane.busy
                cursorShape: Qt.PointingHandCursor
                onClicked: pane.pathSubmitted("/")
            }
        }
        Repeater {
            model: pane.crumbParts(pane.path)
            delegate: Row {
                required property var modelData
                required property int index
                spacing: 3
                Text {
                    visible: index > 0 || !pane.pathIsPosix
                    text: "/"
                    color: Theme.textTertiary
                    font.pixelSize: 11
                }
                Text {
                    text: modelData.label
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    MouseArea {
                        anchors.fill: parent
                        enabled: pane.enabledActions && !pane.busy
                        cursorShape: Qt.PointingHandCursor
                        onClicked: pane.pathSubmitted(modelData.path)
                    }
                }
            }
        }
    }

    // Clicking a column sorts by it; clicking the active column toggles direction.
    function applySort(key) {
        if (pane.fileModel.sortKey === key)
            pane.fileModel.setSort(key, !pane.fileModel.sortAscending)
        else
            pane.fileModel.setSort(key, true)
    }

    // Sortable column header. Mirrors the row delegate's column widths/margins.
    component SortLabel: Text {
        property int sortKey: 0
        readonly property bool activeSort: pane.fileModel && pane.fileModel.sortKey === sortKey
        color: activeSort ? Theme.textSecondary : Theme.textTertiary
        font.pixelSize: 11
        font.letterSpacing: 1
        elide: Text.ElideRight
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: pane.applySort(parent.sortKey)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        spacing: 8
        SortLabel { sortKey: 3; text: "TYPE" + (activeSort ? (pane.fileModel.sortAscending ? " ▲" : " ▼") : ""); Layout.preferredWidth: 36 }
        SortLabel { sortKey: 0; text: "NAME" + (activeSort ? (pane.fileModel.sortAscending ? " ▲" : " ▼") : ""); Layout.fillWidth: true }
        SortLabel { sortKey: 1; text: "SIZE" + (activeSort ? (pane.fileModel.sortAscending ? " ▲" : " ▼") : ""); Layout.preferredWidth: 64; horizontalAlignment: Text.AlignRight }
        SortLabel { sortKey: 2; text: "MODIFIED" + (activeSort ? (pane.fileModel.sortAscending ? " ▲" : " ▼") : ""); Layout.preferredWidth: 96 }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: Theme.bgPrimary
        radius: Theme.radius
        border.width: pane.dropHighlight ? 2 : 1
        border.color: pane.dropHighlight ? Theme.accent : Theme.border

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
                required property string linkTarget
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
                        // Symlinks show their target inline: "link → /path/to/target".
                        text: row.isSymlink && row.linkTarget.length > 0
                              ? row.name + "  → " + row.linkTarget
                              : row.name
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

                    property real pressX: 0
                    property real pressY: 0
                    property bool dragging: false

                    onPressed: function(mouse) {
                        pressX = mouse.x
                        pressY = mouse.y
                        dragging = false
                    }
                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton))
                            return
                        var sp = mapToItem(null, mouse.x, mouse.y)
                        if (!dragging) {
                            if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) <= 8)
                                return
                            dragging = true
                            if (!pane.isSelected(row.name))
                                pane.toggle(row.name, false)
                            pane.dragBegan(pane.side, pane.selected, sp.x, sp.y)
                        } else {
                            pane.dragMoved(sp.x, sp.y)
                        }
                    }
                    onReleased: {
                        if (dragging) {
                            dragging = false
                            pane.dragEnded()
                        }
                    }
                    onClicked: function(mouse) {
                        if (rowMouse.dragging)
                            return  // the press was a drag, not a select
                        var additive = (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)) !== 0
                        pane.toggle(row.name, additive)
                    }
                    onDoubleClicked: { if (row.isDir) pane.openDir(row.name) }
                }
            }
        }

        // Accepts internal drags from the other pane and (optionally) external file
        // drops from Finder. Doesn't interfere with normal clicks — a DropArea only
        // reacts during an active drag.
        DropArea {
            anchors.fill: parent
            keys: ["ceres-files", "text/uri-list"]
            onEntered: function(drag) { pane.dropHighlight = true }
            onExited: pane.dropHighlight = false
            onDropped: function(drop) {
                pane.dropHighlight = false
                if (drop.hasUrls && pane.acceptFileDrops) {
                    pane.filesDropped(drop.urls)
                    drop.accept()
                } else if (drop.source && drop.source.dragNames !== undefined) {
                    if (drop.source.dragSide !== pane.side)
                        pane.itemsDropped(drop.source.dragSide, drop.source.dragNames)
                    drop.accept()
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
