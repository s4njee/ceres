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

                    delegate: Rectangle {
                        required property string id
                        required property string name
                        required property string direction
                        required property int status
                        required property int percent
                        required property string speed
                        required property string statusText
                        required property string error

                        width: ListView.view.width
                        height: 46
                        radius: Theme.radius
                        color: "transparent"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: direction === "up" ? "↑" : "↓"; color: Theme.accent; font.pixelSize: 13 }
                                Text {
                                    Layout.fillWidth: true
                                    text: name
                                    color: Theme.textPrimary
                                    font.family: Theme.mono
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    text: error.length > 0 ? error : (speed.length > 0 ? speed : statusText)
                                    color: status === 3 ? Theme.danger
                                                        : (status === 2 ? Theme.ok : Theme.textTertiary)
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 160
                                }
                                FlatButton {
                                    label: "✕"
                                    active: status === 0 || status === 1   // Queued or Active
                                    onClicked: transfers.cancel(id)
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 4
                                radius: 2
                                color: Theme.bgTertiary
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(100, percent)) / 100
                                    height: parent.height
                                    radius: 2
                                    color: status === 3 ? Theme.danger : (status === 2 ? Theme.ok : Theme.accent)
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
                FlatButton { label: "Close"; primary: true; onClicked: root.open = false }
            }
        }
    }
}
