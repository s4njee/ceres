import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Dual-pane (local ⇆ remote) file browser, backed by the `browse` controller and
// the `transfers` queue. A left sidebar lists saved SSH hosts (the same model the
// Sync window uses); pick one to connect, or type a target. Navigate both sides,
// transfer with the center buttons, and manage remote files (new folder / rename /
// delete). A modal transfers list auto-opens when work is queued.
Item {
    id: tab

    // ---- prompt helper (inline modal) ----
    function promptInput(title, initial, onAccept) {
        promptDialog.title = title
        promptDialog.field = initial
        promptDialog.onAcceptFn = onAccept
        promptDialog.open = true
    }

    function confirmDelete(names, location, onConfirm) {
        deleteConfirm.count = names.length
        deleteConfirm.location = location
        deleteConfirm.onConfirmFn = onConfirm
        deleteConfirm.open = true
    }

    // transient error/status message (cleared after a few seconds)
    property string message: ""
    Timer { id: messageTimer; interval: 5000; onTriggered: tab.message = "" }

    // ---- internal pane→pane drag ----
    // Panes report drags in scene coords; we drive this floating proxy (the actual
    // Drag source the DropAreas see) and convert to tab-local coords to position it.
    function dragBeginAt(side, names, sx, sy) {
        if (!names || names.length === 0)
            return
        var p = tab.mapFromItem(null, sx, sy)
        dragProxy.dragSide = side
        dragProxy.dragNames = names
        dragProxy.x = p.x + 8
        dragProxy.y = p.y + 8
        dragProxy.Drag.active = true
    }
    function dragMoveAt(sx, sy) {
        var p = tab.mapFromItem(null, sx, sy)
        dragProxy.x = p.x + 8
        dragProxy.y = p.y + 8
    }
    function dragEnd() {
        dragProxy.Drag.drop()
        dragProxy.Drag.active = false
    }

    function urlsToPaths(urls) {
        var out = []
        for (var i = 0; i < urls.length; ++i) {
            // Strip "file://"; on Windows that leaves "/C:/..." so drop the leading
            // slash before the drive letter to get a real "C:/..." path.
            var p = ("" + urls[i]).replace(/^file:\/\//, "")
            if (/^\/[A-Za-z]:/.test(p)) p = p.substring(1)
            out.push(decodeURIComponent(p))
        }
        return out
    }

    Item {
        id: dragProxy
        parent: tab
        z: 2000
        width: 130
        height: 26
        visible: Drag.active
        property var dragNames: []
        property string dragSide: ""
        Drag.active: false
        Drag.keys: ["ceres-files"]
        Drag.hotSpot: Qt.point(12, 13)
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: Theme.accent
            opacity: 0.92
            Text {
                anchors.centerIn: parent
                text: dragProxy.dragNames.length + (dragProxy.dragNames.length === 1 ? " item" : " items")
                color: Theme.textPrimary
                font.pixelSize: 11
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---------- saved SSH hosts sidebar ----------
        Rectangle {
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            color: Theme.bgSecondary
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Text { text: "SAVED SSH HOSTS"; color: Theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1 }

                ListView {
                    id: hostsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: controller.sshHosts
                    ScrollBar.vertical: ScrollBar {}

                    Text {
                        anchors.centerIn: parent
                        visible: controller.sshHosts.count === 0
                        text: "Ø  No saved SSH hosts"
                        color: Theme.textTertiary
                        font.pixelSize: 12
                    }

                    delegate: Rectangle {
                        id: hostRow
                        required property string target
                        required property string summary
                        required property int jobCount

                        readonly property bool active: target === browse.target && browse.connected
                        width: ListView.view.width
                        height: 42
                        radius: Theme.radius
                        color: active ? Theme.bgTertiary
                                      : (hostMouse.containsMouse ? Theme.bgTertiary : "transparent")
                        border.width: active ? 1 : 0
                        border.color: Theme.accent

                        MouseArea {
                            id: hostMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { targetField.text = target; browse.connectHost(target) }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8
                            Rectangle {
                                width: 7; height: 7; radius: 4
                                color: hostRow.active ? Theme.accent : Theme.ok
                                Layout.alignment: Qt.AlignVCenter
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text { text: target; color: Theme.textPrimary; font.family: Theme.mono; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: summary; color: Theme.textTertiary; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            }
                            Text { text: jobCount; visible: jobCount > 1; color: Theme.textTertiary; font.pixelSize: 10 }
                        }
                    }
                }

                FlatButton { label: "Disconnect"; Layout.fillWidth: true; visible: browse.connected; danger: true; outline: true; onClicked: browse.disconnectHost() }
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }

        // ---------- main browse area ----------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // top: manual connect + status + transfers
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 8

                Field {
                    id: targetField
                    Layout.preferredWidth: 240
                    placeholderText: "user@host"
                    onAccepted: browse.connectHost(text)
                }
                FlatButton {
                    label: browse.connected ? "Reconnect" : "Connect"
                    primary: !browse.connected
                    active: targetField.text.trim().length > 0
                    onClicked: browse.connectHost(targetField.text)
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: tab.message.length > 0
                    text: tab.message
                    color: Theme.warning
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.maximumWidth: 260
                }
                FlatButton {
                    label: transfers.activeCount > 0 ? "Transfers (" + transfers.activeCount + ")" : "Transfers"
                    onClicked: transfersDialog.open = true
                }
            }

            // dual panes (equal width + height)
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 10

                FileBrowserPane {
                    id: localPane
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1   // equal share with the remote pane
                    title: "LOCAL"
                    fileModel: browse.localFiles
                    path: browse.localPath
                    allowContext: true
                    side: "local"
                    onUpRequested: browse.localUp()
                    onRefreshRequested: browse.localRefresh()
                    onPathSubmitted: function(path) { browse.setLocalPath(path); clearSelection() }
                    onOpenDir: function(name) { browse.localCd(name); clearSelection() }
                    onContextMenuRequested: localMenu.popup()
                    onDragBegan: function(side, names, sx, sy) { tab.dragBeginAt(side, names, sx, sy) }
                    onDragMoved: function(sx, sy) { tab.dragMoveAt(sx, sy) }
                    onDragEnded: tab.dragEnd()
                    // Dropping remote items here downloads them to the current local dir.
                    onItemsDropped: function(fromSide, names) {
                        if (fromSide === "remote") { browse.download(names); remotePane.clearSelection() }
                    }
                }

                FileBrowserPane {
                    id: remotePane
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1   // equal share with the local pane
                    title: browse.connected ? "REMOTE · " + browse.target : "REMOTE"
                    fileModel: browse.remoteFiles
                    path: browse.remotePath
                    busy: browse.busy
                    enabledActions: browse.connected
                    allowContext: browse.connected
                    side: "remote"
                    acceptFileDrops: browse.connected
                    onUpRequested: browse.remoteUp()
                    onRefreshRequested: browse.remoteRefresh()
                    onPathSubmitted: function(path) { browse.setRemotePath(path); clearSelection() }
                    onOpenDir: function(name) { browse.remoteCd(name); clearSelection() }
                    onContextMenuRequested: remoteMenu.popup()
                    onDragBegan: function(side, names, sx, sy) { tab.dragBeginAt(side, names, sx, sy) }
                    onDragMoved: function(sx, sy) { tab.dragMoveAt(sx, sy) }
                    onDragEnded: tab.dragEnd()
                    // Dropping local items (or Finder files) here uploads to the current remote dir.
                    onItemsDropped: function(fromSide, names) {
                        if (fromSide === "local") { browse.upload(names); localPane.clearSelection() }
                    }
                    onFilesDropped: function(urls) { browse.uploadFiles(tab.urlsToPaths(urls)) }
                }
            }

            // bottom transfer bar
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                spacing: 10
                Item { Layout.fillWidth: true }
                FlatButton {
                    label: "↑ Upload"
                    primary: true
                    active: browse.connected && localPane.selected.length > 0
                    onClicked: { browse.upload(localPane.selected); localPane.clearSelection() }
                }
                FlatButton {
                    label: "↓ Download"
                    primary: true
                    active: browse.connected && remotePane.selected.length > 0
                    onClicked: { browse.download(remotePane.selected); remotePane.clearSelection() }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    // ---------- right-click context menus (shared item style) ----------
    component CtxItem: MenuItem {
        id: ctx
        property bool dangerous: false
        implicitHeight: 28
        implicitWidth: 160
        contentItem: Text {
            text: ctx.text
            color: ctx.enabled ? (ctx.dangerous ? Theme.danger : Theme.textPrimary) : Theme.textTertiary
            font.pixelSize: 12
            leftPadding: 8
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: ctx.highlighted ? Theme.bgTertiary : "transparent"
            radius: 4
        }
    }
    component CtxMenu: Menu {
        background: Rectangle {
            implicitWidth: 160
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderStrong
            radius: Theme.radius
        }
    }

    CtxMenu {
        id: remoteMenu
        CtxItem {
            text: "New folder"
            onTriggered: tab.promptInput("New folder name", "", function(v) { browse.mkdirRemote(v) })
        }
        CtxItem {
            text: "Rename"
            enabled: remotePane.selected.length === 1
            onTriggered: tab.promptInput("Rename to", remotePane.selected[0],
                                         function(v) { browse.renameRemote(remotePane.selected[0], v); remotePane.clearSelection() })
        }
        CtxItem {
            text: "Delete"
            dangerous: true
            enabled: remotePane.selected.length > 0
            onTriggered: tab.confirmDelete(remotePane.selected, browse.remotePath,
                                           function() { browse.deleteRemote(remotePane.selected); remotePane.clearSelection() })
        }
    }

    CtxMenu {
        id: localMenu
        CtxItem {
            text: "New folder"
            onTriggered: tab.promptInput("New folder name", "", function(v) { browse.mkdirLocal(v) })
        }
        CtxItem {
            text: "Rename"
            enabled: localPane.selected.length === 1
            onTriggered: tab.promptInput("Rename to", localPane.selected[0],
                                         function(v) { browse.renameLocal(localPane.selected[0], v); localPane.clearSelection() })
        }
        CtxItem {
            text: "Delete"
            dangerous: true
            enabled: localPane.selected.length > 0
            onTriggered: tab.confirmDelete(localPane.selected, browse.localPath,
                                           function() { browse.deleteLocal(localPane.selected); localPane.clearSelection() })
        }
    }

    Connections {
        target: browse
        function onAuthRequired(host, user) { sshAuthDialog.show(host, user) }
        function onHostKeyChanged(host) { knownHostDialog.show(host) }
        function onErrorOccurred(msg) { tab.message = msg; messageTimer.restart() }
    }
    Connections {
        target: transfers
        function onEnqueued() { transfersDialog.open = true }
    }

    // ---------- dialogs ----------
    SshPasswordDialog {
        id: sshAuthDialog
        onCanceled: open = false
        onSubmitted: function(user, password, remember) {
            browse.connectWithPassword(user, password, remember)
        }
    }

    KnownHostChangedDialog {
        id: knownHostDialog
        onCanceled: open = false
        onConfirmed: {
            open = false
            browse.repairKnownHostAndRetry()
        }
    }

    TransfersDialog { id: transfersDialog }

    // delete confirm (used for both panes via confirmDelete)
    Rectangle {
        id: deleteConfirm
        property bool open: false
        property int count: 0
        property string location
        property var onConfirmFn: null
        anchors.fill: parent
        visible: open
        color: "#cc000000"
        focus: open
        Keys.onEscapePressed: open = false
        MouseArea { anchors.fill: parent }
        Rectangle {
            anchors.centerIn: parent
            width: 380
            height: dcCol.implicitHeight + 36
            radius: Theme.radius
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderStrong
            ColumnLayout {
                id: dcCol
                x: 18; y: 18
                width: parent.width - 36
                spacing: 12
                Text { text: "Delete items"; color: Theme.textPrimary; font.pixelSize: 15 }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: "Permanently delete " + deleteConfirm.count
                          + " item(s) from " + deleteConfirm.location + "? This cannot be undone."
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    FlatButton { label: "Cancel"; onClicked: deleteConfirm.open = false }
                    FlatButton {
                        label: "Delete"
                        danger: true
                        onClicked: {
                            if (deleteConfirm.onConfirmFn) deleteConfirm.onConfirmFn()
                            deleteConfirm.open = false
                        }
                    }
                }
            }
        }
    }

    // generic text prompt (mkdir / rename)
    Rectangle {
        id: promptDialog
        property bool open: false
        property string title
        property string field
        property var onAcceptFn: null
        function accept() { if (onAcceptFn && field.trim().length > 0) onAcceptFn(field.trim()); open = false }

        anchors.fill: parent
        visible: open
        color: "#cc000000"
        focus: open
        Keys.onEscapePressed: open = false
        MouseArea { anchors.fill: parent }
        onOpenChanged: if (open) promptField.forceActiveFocus()
        Rectangle {
            anchors.centerIn: parent
            width: 380
            height: pCol.implicitHeight + 36
            radius: Theme.radius
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderStrong
            ColumnLayout {
                id: pCol
                x: 18; y: 18
                width: parent.width - 36
                spacing: 12
                Text { text: promptDialog.title; color: Theme.textPrimary; font.pixelSize: 15 }
                Field {
                    id: promptField
                    Layout.fillWidth: true
                    text: promptDialog.field
                    onTextChanged: promptDialog.field = text
                    onAccepted: promptDialog.accept()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    FlatButton { label: "Cancel"; onClicked: promptDialog.open = false }
                    FlatButton { label: "OK"; onClicked: promptDialog.accept() }
                }
            }
        }
    }
}
