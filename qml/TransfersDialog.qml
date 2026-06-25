import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Modal list of queued/active/finished transfers, bound to `transfers.model`.
// Auto-opened by BrowseTab when a transfer is enqueued; also reopenable from the
// toolbar. Each row shows direction, name, a progress bar, speed, status, and a
// per-row cancel. Modeled on DeleteConfirmDialog (scrim + centered panel).
Rectangle {
    id: root
    property bool open: false
    property bool historyMode: false
    property var historyItems: []

    function refreshHistory() { historyItems = transfers.history() }
    onOpenChanged: if (open && historyMode) refreshHistory()

    Connections {
        target: transfers
        function onHistoryChanged() { if (root.historyMode) root.refreshHistory() }
    }

    anchors.fill: parent
    visible: open
    color: "#cc000000"
    focus: open
    Keys.onEscapePressed: root.open = false

    MouseArea { anchors.fill: parent }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 560)
        height: Math.min(parent.height - 80, 460)
        radius: Theme.radius
        color: Theme.bgSecondary
        border.width: 1
        border.color: Theme.borderStrong

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: root.historyMode ? "History" : "Transfers"; color: Theme.textPrimary; font.pixelSize: 15 }
                FlatButton {
                    label: root.historyMode ? "Active" : "History"
                    onClicked: { root.historyMode = !root.historyMode; if (root.historyMode) root.refreshHistory() }
                }
                FlatButton {
                    label: "Clear"
                    visible: root.historyMode
                    onClicked: transfers.clearHistory()
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: transfers.activeCount > 0 ? transfers.activeCount + " active" : ""
                    color: Theme.textTertiary
                    font.pixelSize: 12
                }
                // Transfer-rate cap. Blank/0 = unlimited; applied to newly started transfers.
                Text { text: "limit"; color: Theme.textTertiary; font.pixelSize: 12 }
                Field {
                    id: limitField
                    Layout.preferredWidth: 56
                    implicitHeight: 24
                    horizontalAlignment: TextInput.AlignRight
                    placeholderText: "∞"
                    text: transfers.rateLimitKBps > 0 ? transfers.rateLimitKBps : ""
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 0; top: 1000000 }
                    onEditingFinished: transfers.rateLimitKBps = parseInt(text || "0")
                }
                Text { text: "KB/s"; color: Theme.textTertiary; font.pixelSize: 12 }
            }

            // Overwrite policy for files already present at the destination.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: "On conflict:"; color: Theme.textTertiary; font.pixelSize: 12 }
                Chip {
                    label: "overwrite"
                    active: transfers.overwritePolicy === 0
                    tooltip: "Replace destination files (rsync default)"
                    onToggled: transfers.overwritePolicy = 0
                }
                Chip {
                    label: "skip existing"
                    active: transfers.overwritePolicy === 1
                    tooltip: "Never touch files already on the destination (--ignore-existing)"
                    onToggled: transfers.overwritePolicy = 1
                }
                Chip {
                    label: "newer only"
                    active: transfers.overwritePolicy === 2
                    tooltip: "Only replace when the source is newer (--update)"
                    onToggled: transfers.overwritePolicy = 2
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPrimary
                radius: Theme.radius
                border.width: 1
                border.color: Theme.border

                // Persistent log of finished transfers (most-recent first).
                ListView {
                    id: historyList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    visible: root.historyMode
                    model: root.historyItems
                    spacing: 2
                    ScrollBar.vertical: ScrollBar {}

                    Text {
                        anchors.centerIn: parent
                        visible: historyList.count === 0
                        text: "No past transfers"
                        color: Theme.textTertiary
                        font.pixelSize: 12
                    }

                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width
                        spacing: 8
                        Text {
                            text: modelData.direction === "up" ? "↑" : "↓"
                            color: Theme.accent
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: Theme.textPrimary
                            font.family: Theme.mono
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: modelData.status
                            color: modelData.status === "Done" ? Theme.ok
                                  : (modelData.status === "Failed" ? Theme.danger : Theme.textTertiary)
                            font.pixelSize: 11
                        }
                        Text {
                            text: (modelData.time || "").replace("T", " ").slice(0, 16)
                            color: Theme.textTertiary
                            font.pixelSize: 10
                            Layout.preferredWidth: 104
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    visible: !root.historyMode
                    model: transfers.model
                    spacing: 2
                    ScrollBar.vertical: ScrollBar {}

                    Text {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        text: "No transfers yet"
                        color: Theme.textTertiary
                        font.pixelSize: 12
                    }

                    delegate: ColumnLayout {
                        id: tRow
                        required property string id
                        required property string name
                        required property string direction
                        required property int status
                        required property int percent
                        required property string speed
                        required property string summary
                        required property string statusText
                        required property string error
                        required property var files
                        required property int fileCount

                        property bool expanded: false
                        readonly property bool expandable: fileCount > 0

                        width: ListView.view.width
                        spacing: 3

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            Layout.topMargin: 4
                            spacing: 8
                            Text {
                                text: tRow.expandable ? (tRow.expanded ? "▾" : "▸") : ""
                                color: Theme.textTertiary
                                font.pixelSize: 11
                                Layout.preferredWidth: 10
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: tRow.expandable
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: tRow.expanded = !tRow.expanded
                                }
                            }
                            Text { text: tRow.direction === "up" ? "↑" : "↓"; color: Theme.accent; font.pixelSize: 13 }
                            Text {
                                Layout.fillWidth: true
                                text: tRow.name
                                color: Theme.textPrimary
                                font.family: Theme.mono
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: tRow.expandable
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: tRow.expanded = !tRow.expanded
                                }
                            }
                            Text {
                                visible: tRow.fileCount > 0
                                text: tRow.fileCount + (tRow.fileCount === 1 ? " file" : " files")
                                color: Theme.textTertiary
                                font.pixelSize: 10
                            }
                            Text {
                                // Error > done-summary > live speed+ETA > status text.
                                text: {
                                    if (tRow.error.length > 0) return tRow.error
                                    if (tRow.status === 2 && tRow.summary.length > 0) return tRow.summary
                                    if (tRow.status === 1 && tRow.speed.length > 0) return tRow.speed
                                    return tRow.statusText
                                }
                                color: tRow.status === 3 ? Theme.danger
                                                         : (tRow.status === 2 ? Theme.ok : Theme.textTertiary)
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.maximumWidth: 200
                            }
                            FlatButton {
                                label: "❚❚"   // pause
                                visible: tRow.status === 0 || tRow.status === 1   // Queued or Active
                                onClicked: transfers.pause(tRow.id)
                            }
                            FlatButton {
                                label: "▶"    // resume
                                visible: tRow.status === 5   // Paused
                                onClicked: transfers.resume(tRow.id)
                            }
                            FlatButton {
                                label: "↻"    // retry
                                visible: tRow.status === 3 || tRow.status === 4   // Failed or Cancelled
                                onClicked: transfers.retry(tRow.id)
                            }
                            FlatButton {
                                label: "✕"
                                visible: tRow.status === 0 || tRow.status === 1 || tRow.status === 5
                                onClicked: transfers.cancel(tRow.id)
                            }
                        }

                        // aggregate bar
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            height: 4
                            radius: 2
                            color: Theme.bgTertiary
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(100, tRow.percent)) / 100
                                height: parent.height
                                radius: 2
                                color: tRow.status === 3 ? Theme.danger
                                     : (tRow.status === 2 ? Theme.ok
                                     : (tRow.status === 5 ? Theme.textTertiary : Theme.accent))
                            }
                        }

                        // per-file detail (expandable tree populated by the live rsync stream)
                        ColumnLayout {
                            visible: tRow.expanded
                            Layout.fillWidth: true
                            Layout.leftMargin: 26
                            Layout.rightMargin: 8
                            Layout.bottomMargin: 4
                            spacing: 2
                            Repeater {
                                model: tRow.files
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Item {
                                        Layout.preferredWidth: Math.max(0, modelData.depth || 0) * 14
                                        height: 1
                                    }
                                    Text {
                                        text: modelData.isDir ? "▾" : "•"
                                        color: Theme.textTertiary
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 10
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: modelData.isDir ? Theme.textPrimary : Theme.textSecondary
                                        font.family: Theme.mono
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                    }
                                    Rectangle {
                                        visible: !modelData.isDir
                                        Layout.preferredWidth: 90
                                        height: 3
                                        radius: 1.5
                                        color: Theme.bgTertiary
                                        Rectangle {
                                            width: parent.width * Math.max(0, Math.min(100, modelData.percent)) / 100
                                            height: parent.height
                                            radius: 1.5
                                            // Muted bar for files rsync skipped (already current); ok-green for real transfers.
                                            color: modelData.upToDate ? Theme.textTertiary : (modelData.percent >= 100 ? Theme.ok : Theme.accent)
                                        }
                                    }
                                    Text {
                                        visible: !modelData.isDir
                                        text: modelData.upToDate ? "" : modelData.percent + "%"
                                        color: Theme.textTertiary
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 32
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    Text {
                                        visible: !modelData.isDir
                                        text: modelData.upToDate ? "up to date" : modelData.rate
                                        color: Theme.textTertiary
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 70
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                FlatButton { label: "Clear finished"; onClicked: transfers.clearCompleted() }
                Item { Layout.fillWidth: true }
                FlatButton { label: "Close"; onClicked: root.open = false }
            }
        }
    }
}
