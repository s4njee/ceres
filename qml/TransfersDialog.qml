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
                Text { text: "Transfers"; color: Theme.textPrimary; font.pixelSize: 15 }
                Item { Layout.fillWidth: true }
                Text {
                    text: transfers.activeCount > 0 ? transfers.activeCount + " active" : ""
                    color: Theme.textTertiary
                    font.pixelSize: 12
                }
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
                        required property string statusText
                        required property string error
                        required property var files
                        required property int fileCount

                        property bool expanded: false
                        readonly property bool expandable: fileCount > 1

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
                                text: tRow.error.length > 0 ? tRow.error
                                      : (tRow.speed.length > 0 ? tRow.speed : tRow.statusText)
                                color: tRow.status === 3 ? Theme.danger
                                                         : (tRow.status === 2 ? Theme.ok : Theme.textTertiary)
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.maximumWidth: 150
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

                        // per-file detail (expandable)
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
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: Theme.textSecondary
                                        font.family: Theme.mono
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 90
                                        height: 3
                                        radius: 1.5
                                        color: Theme.bgTertiary
                                        Rectangle {
                                            width: parent.width * Math.max(0, Math.min(100, modelData.percent)) / 100
                                            height: parent.height
                                            radius: 1.5
                                            color: modelData.percent >= 100 ? Theme.ok : Theme.accent
                                        }
                                    }
                                    Text {
                                        text: modelData.percent + "%"
                                        color: Theme.textTertiary
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 32
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    Text {
                                        text: modelData.rate
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
