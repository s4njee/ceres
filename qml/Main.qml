import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1000
    height: 680
    visible: true
    title: qsTr("Ceres")
    color: theme.bgPrimary

    property bool archiveOn: true
    property bool compressOn: false
    property bool deleteOn: false
    property bool checksumOn: false
    property bool confirmOpen: false

    // High-contrast dark theme (Claude Code / Electron dev-tool aesthetic).
    // Black main surface, dark-grey sidebar. Tokens kept here for now; promote
    // to a QML singleton once components are extracted.
    QtObject {
        id: theme
        readonly property color bgPrimary: "#000000"
        readonly property color bgSecondary: "#1a1a1a"
        readonly property color bgTertiary: "#242424"
        readonly property color textPrimary: "#ededed"
        readonly property color textSecondary: "#9a9a9a"
        readonly property color textTertiary: "#6a6a6a"
        readonly property color border: "#2b2b2b"
        readonly property color borderStrong: "#3a3a3a"
        readonly property color accent: "#d97757"
        readonly property color ok: "#3fb950"
        readonly property color info: "#58a6ff"
        readonly property color danger: "#f85149"
        readonly property color warning: "#d29922"
        readonly property int radius: 6
        readonly property string mono: "Menlo"
    }

    component Chip: Rectangle {
        property string label
        property bool active: false
        property bool warn: false
        signal toggled()
        implicitHeight: 26
        implicitWidth: chipRow.implicitWidth + 18
        radius: theme.radius
        color: active ? theme.bgTertiary : "transparent"
        border.width: 1
        border.color: active ? theme.borderStrong : theme.border
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 5
            Rectangle {
                visible: warn
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: theme.warning
            }
            Text { text: label; font.pixelSize: 12; color: active ? theme.textPrimary : theme.textSecondary }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.toggled() }
    }

    component FlatButton: Rectangle {
        property string label
        property bool primary: false
        property bool danger: false
        property bool active: true
        signal clicked()
        implicitHeight: 30
        implicitWidth: btnText.implicitWidth + 24
        radius: theme.radius
        opacity: active ? 1.0 : 0.45
        color: danger ? theme.danger : (primary ? theme.accent : "transparent")
        border.width: (primary || danger) ? 0 : 1
        border.color: theme.borderStrong
        Text {
            id: btnText
            anchors.centerIn: parent
            text: label
            font.pixelSize: 12
            color: (primary || danger) ? "#160a06" : theme.textPrimary
        }
        MouseArea {
            anchors.fill: parent
            enabled: parent.active
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------- top strip ----------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: theme.bgSecondary
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 8
                Text { text: "ceres"; color: theme.textPrimary; font.family: theme.mono; font.pixelSize: 14; font.bold: true }
                Item { Layout.fillWidth: true }
                Rectangle {
                    radius: theme.radius
                    color: "transparent"
                    border.width: 1
                    border.color: theme.border
                    implicitHeight: 24
                    implicitWidth: hostRow.implicitWidth + 18
                    RowLayout {
                        id: hostRow
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle { width: 7; height: 7; radius: 4; color: theme.ok }
                        Text {
                            text: controller.hostName + "  ·  " + controller.hostAddress
                            color: theme.textSecondary
                            font.family: theme.mono
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }

        // ---------- body ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // sidebar
            Rectangle {
                Layout.preferredWidth: 210
                Layout.fillHeight: true
                color: theme.bgSecondary
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 4

                    Text { text: "JOBS"; color: theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1 }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 34
                        radius: theme.radius
                        color: theme.bgTertiary
                        border.width: 1
                        border.color: theme.borderStrong
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                            Rectangle { width: 7; height: 7; radius: 4; color: controller.running ? theme.warning : theme.ok }
                            Text { text: "Current sync"; color: theme.textPrimary; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }

                    Text { text: "ON YOUR NETWORK"; color: theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1; Layout.topMargin: 14 }
                    Text { text: "Discovery — coming soon"; color: theme.textTertiary; font.pixelSize: 12 }

                    Item { Layout.fillHeight: true }

                    FlatButton { Layout.fillWidth: true; label: "+  New sync"; active: false }
                    Text {
                        text: controller.rsyncSummary
                        color: controller.usingOpenRsync ? theme.warning : theme.textTertiary
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: theme.border }

            // main panel (black)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                spacing: 12

                Text { text: "Untitled sync"; color: theme.textPrimary; font.pixelSize: 16 }

                GridLayout {
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 8
                    Layout.fillWidth: true

                    Text { text: "From"; color: theme.textSecondary; font.pixelSize: 12 }
                    TextField {
                        id: fromField
                        Layout.fillWidth: true
                        placeholderText: qsTr("source path (trailing / copies contents)")
                        color: theme.textPrimary
                        placeholderTextColor: theme.textTertiary
                        font.family: theme.mono
                        font.pixelSize: 13
                        background: Rectangle {
                            color: theme.bgTertiary; radius: theme.radius
                            border.width: 1; border.color: fromField.activeFocus ? theme.accent : theme.border
                        }
                    }

                    Text { text: "To"; color: theme.textSecondary; font.pixelSize: 12 }
                    TextField {
                        id: toField
                        Layout.fillWidth: true
                        placeholderText: qsTr("destination path")
                        color: theme.textPrimary
                        placeholderTextColor: theme.textTertiary
                        font.family: theme.mono
                        font.pixelSize: 13
                        background: Rectangle {
                            color: theme.bgTertiary; radius: theme.radius
                            border.width: 1; border.color: toField.activeFocus ? theme.accent : theme.border
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Chip { label: "archive"; active: root.archiveOn; onToggled: root.archiveOn = !root.archiveOn }
                    Chip { label: "compress"; active: root.compressOn; onToggled: root.compressOn = !root.compressOn }
                    Chip { label: "delete extras"; warn: true; active: root.deleteOn; onToggled: root.deleteOn = !root.deleteOn }
                    Chip { label: "checksum"; active: root.checksumOn; onToggled: root.checksumOn = !root.checksumOn }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    readonly property bool canStart: !controller.running && fromField.text.length > 0 && toField.text.length > 0
                    FlatButton {
                        label: "Preview"
                        active: parent.canStart
                        onClicked: controller.preview(fromField.text, toField.text,
                                                      root.archiveOn, root.compressOn, root.deleteOn, root.checksumOn)
                    }
                    FlatButton {
                        label: controller.running ? "Running…" : "Run sync"
                        primary: true
                        active: parent.canStart
                        onClicked: {
                            if (root.deleteOn)
                                root.confirmOpen = true
                            else
                                controller.run(fromField.text, toField.text,
                                               root.archiveOn, root.compressOn, root.deleteOn, root.checksumOn)
                        }
                    }
                    Item { Layout.fillWidth: true }
                    FlatButton { label: "Cancel"; active: controller.running; onClicked: controller.cancel() }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "PREVIEW"; color: theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1 }
                    Text { text: controller.changes.count + " changes"; color: theme.textSecondary; font.pixelSize: 12 }
                    Text {
                        visible: controller.changes.deletions > 0
                        text: "· " + controller.changes.deletions + " to delete"
                        color: theme.danger
                        font.pixelSize: 12
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius
                    color: "transparent"
                    border.width: 1
                    border.color: theme.border
                    ListView {
                        id: list
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        model: controller.changes
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Item {
                            width: ListView.view.width
                            height: 24
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 10
                                Text {
                                    text: code
                                    color: isDelete ? theme.danger : (isNew ? theme.ok : theme.info)
                                    font.family: theme.mono
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 96
                                }
                                Text {
                                    text: path
                                    color: theme.textPrimary
                                    font.family: theme.mono
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 5
                            radius: 3
                            color: theme.bgTertiary
                            Rectangle {
                                height: parent.height
                                radius: 3
                                width: parent.width * Math.max(0, Math.min(100, controller.percent)) / 100
                                color: theme.accent
                            }
                        }
                        Text { text: controller.status; color: theme.textSecondary; font.pixelSize: 12 }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 88
                        clip: true
                        TextArea {
                            readOnly: true
                            text: controller.log
                            color: theme.textTertiary
                            font.family: theme.mono
                            font.pixelSize: 11
                            wrapMode: TextEdit.NoWrap
                            background: Rectangle { color: "transparent" }
                        }
                    }
                }
            }
        }
    }

    // ---------- delete confirmation gate ----------
    Rectangle {
        anchors.fill: parent
        visible: root.confirmOpen
        color: "#cc000000"
        MouseArea { anchors.fill: parent }  // swallow clicks behind the modal

        Rectangle {
            anchors.centerIn: parent
            width: 400
            height: panelCol.implicitHeight + 36
            radius: theme.radius
            color: theme.bgSecondary
            border.width: 1
            border.color: theme.borderStrong

            ColumnLayout {
                id: panelCol
                x: 18
                y: 18
                width: parent.width - 36
                spacing: 12

                RowLayout {
                    spacing: 8
                    Rectangle { width: 8; height: 8; radius: 4; color: theme.danger; Layout.alignment: Qt.AlignVCenter }
                    Text { text: "Delete extras is on"; color: theme.textPrimary; font.pixelSize: 15 }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: theme.textSecondary
                    font.pixelSize: 12
                    text: (controller.changes.deletions > 0
                           ? controller.changes.deletions + " file(s) in the destination will be permanently deleted."
                           : "Files in the destination that aren't in the source will be permanently deleted.")
                          + " This cannot be undone — preview first to see exactly which."
                }
                Text {
                    Layout.fillWidth: true
                    color: theme.textTertiary
                    font.family: theme.mono
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    text: fromField.text + "  →  " + toField.text
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    FlatButton { label: "Cancel"; onClicked: root.confirmOpen = false }
                    FlatButton {
                        label: "Sync and delete"
                        danger: true
                        onClicked: {
                            root.confirmOpen = false
                            controller.run(fromField.text, toField.text,
                                           root.archiveOn, root.compressOn, root.deleteOn, root.checksumOn)
                        }
                    }
                }
            }
        }
    }
}
