import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1000
    height: 680
    visible: true
    title: qsTr("Ceres")
    color: Theme.bgPrimary

    property bool archiveOn: true
    property bool compressOn: false
    property bool deleteOn: false
    property bool checksumOn: false
    property bool confirmOpen: false
    property string scheduleKind: "manual"
    property int weekday: 0

    function pad(n) { return (n < 10 ? "0" : "") + n }
    function endpointKind(p) { return controller.endpointKind(p || "") }
    function looksDaemon(p) { return endpointKind(p) === "daemon" }
    function looksRemote(p) {
        if (!p) return false
        var kind = endpointKind(p)
        return kind === "ssh" || kind === "daemon"
    }
    function looksSsh(p) { return endpointKind(p) === "ssh" }

    // Tab-complete a path field: local paths complete synchronously; remote
    // (user@host:) targets complete over ssh and arrive via onRemoteCompleted.
    function completePath(field) {
        var t = field.text
        if (t.length === 0 || looksDaemon(t)) return
        if (looksSsh(t)) {
            completer.completeRemote(t, sshKeyField.text, parseInt(sshPortField.text) || 0)
        } else {
            var c = completer.completeLocal(t)
            if (c.length > 0 && c !== t) { field.text = c; field.cursorPosition = c.length }
        }
    }

    readonly property bool showSsh: looksSsh(fromField.text) || looksSsh(toField.text)
    readonly property bool showDaemon: looksDaemon(fromField.text) || looksDaemon(toField.text)

    function jobMap() {
        return {
            name: nameField.text,
            source: fromField.text,
            destination: toField.text,
            archive: root.archiveOn,
            compress: root.compressOn,
            deleteExtras: root.deleteOn,
            checksum: root.checksumOn,
            maxDelete: parseInt(maxDeleteField.text) || 0,
            sshKey: sshKeyField.text,
            sshPort: parseInt(sshPortField.text) || 0,
            daemonPassword: daemonPwField.text,
            schedule: root.scheduleKind,
            intervalMinutes: parseInt(intervalField.text) || 60,
            atHour: parseInt(timeField.text.split(":")[0]) || 0,
            atMinute: parseInt(timeField.text.split(":")[1]) || 0,
            weekday: root.weekday
        }
    }

    Connections {
        target: controller
        function onJobLoaded(job) {
            nameField.text = job.name && job.name.length > 0 ? job.name : "Untitled sync"
            fromField.text = job.source || ""
            toField.text = job.destination || ""
            root.archiveOn = job.archive
            root.compressOn = job.compress
            root.deleteOn = job.deleteExtras
            root.checksumOn = job.checksum
            maxDeleteField.text = job.maxDelete ? String(job.maxDelete) : ""
            sshKeyField.text = job.sshKey || ""
            sshPortField.text = job.sshPort ? String(job.sshPort) : ""
            daemonPwField.text = ""
            root.scheduleKind = job.schedule || "manual"
            intervalField.text = String(job.intervalMinutes || 60)
            timeField.text = pad(job.atHour) + ":" + pad(job.atMinute)
            root.weekday = job.weekday || 0
            controller.changes.clear()
        }
    }

    Connections {
        target: completer
        function onRemoteCompleted(input, completion) {
            if (completion.length === 0) return
            if (fromField.text === input) { fromField.text = completion; fromField.cursorPosition = completion.length }
            else if (toField.text === input) { toField.text = completion; toField.cursorPosition = completion.length }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------- top strip ----------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.bgSecondary
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 8
                Text { text: "ceres"; color: Theme.textPrimary; font.family: Theme.mono; font.pixelSize: 14; font.bold: true }
                Item { Layout.fillWidth: true }
                Rectangle {
                    radius: Theme.radius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.border
                    implicitHeight: 24
                    implicitWidth: hostRow.implicitWidth + 18
                    RowLayout {
                        id: hostRow
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle { width: 7; height: 7; radius: 4; color: Theme.ok }
                        Text {
                            text: controller.hostName + "  ·  " + controller.hostAddress
                            color: Theme.textSecondary
                            font.family: Theme.mono
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // ---------- body ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // sidebar
            Rectangle {
                Layout.preferredWidth: 210
                Layout.fillHeight: true
                color: Theme.bgSecondary
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Text { text: "JOBS"; color: Theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1 }

                    ListView {
                        id: jobsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: controller.jobs
                        ScrollBar.vertical: ScrollBar {}

                        Text {
                            anchors.centerIn: parent
                            visible: controller.jobs.count === 0
                            text: "No saved jobs yet"
                            color: Theme.textTertiary
                            font.pixelSize: 12
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 42
                            radius: Theme.radius
                            color: (id === controller.currentId) ? Theme.bgTertiary : "transparent"
                            border.width: (id === controller.currentId) ? 1 : 0
                            border.color: Theme.borderStrong

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: controller.loadJob(id)
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 4
                                spacing: 8
                                Rectangle { width: 7; height: 7; radius: 4; color: Theme.ok; Layout.alignment: Qt.AlignVCenter }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: name; color: Theme.textPrimary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: summary; color: Theme.textTertiary; font.family: Theme.mono; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                }
                                Item {
                                    implicitWidth: 20
                                    implicitHeight: 20
                                    Layout.alignment: Qt.AlignVCenter
                                    Text { anchors.centerIn: parent; text: "×"; color: Theme.textTertiary; font.pixelSize: 15 }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: controller.deleteJob(id) }
                                }
                            }
                        }
                    }

                    FlatButton { Layout.fillWidth: true; label: "+  New sync"; onClicked: controller.newJob() }

                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 4 }

                    Text { text: "ON YOUR NETWORK"; color: Theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: "Discoverable"; color: Theme.textSecondary; font.pixelSize: 12; Layout.fillWidth: true }
                        Text { text: controller.discoverable ? "visible" : "hidden"; color: Theme.textTertiary; font.pixelSize: 10; Layout.alignment: Qt.AlignVCenter }
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            width: 30; height: 16; radius: 8
                            color: controller.discoverable ? Theme.accent : Theme.bgTertiary
                            border.width: controller.discoverable ? 0 : 1
                            border.color: Theme.border
                            Rectangle {
                                width: 12; height: 12; radius: 6
                                anchors.verticalCenter: parent.verticalCenter
                                x: controller.discoverable ? parent.width - 14 : 2
                                color: controller.discoverable ? "#160a06" : Theme.textTertiary
                                Behavior on x { NumberAnimation { duration: 120 } }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: controller.discoverable = !controller.discoverable }
                        }
                    }

                    ListView {
                        id: peersList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 150)
                        clip: true
                        spacing: 2
                        model: controller.peers
                        ScrollBar.vertical: ScrollBar {}

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 6
                            visible: controller.peers.count === 0
                            text: "No machines found"
                            color: Theme.textTertiary
                            font.pixelSize: 12
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 40
                            radius: Theme.radius
                            color: "transparent"
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    toField.text = daemon ? ("rsync://" + address + "/") : (address + ":")
                                    toField.forceActiveFocus()
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 8
                                Rectangle { width: 7; height: 7; radius: 4; color: Theme.ok; Layout.alignment: Qt.AlignVCenter }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: name; color: Theme.textPrimary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: address + " · " + os; color: Theme.textTertiary; font.family: Theme.mono; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                }
                                Text { text: accepts; color: Theme.textTertiary; font.pixelSize: 10 }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Field {
                            id: hostAddField
                            Layout.fillWidth: true
                            font.pixelSize: 12
                            placeholderText: qsTr("add by host / IP")
                            onAccepted: { controller.addPeerByHost(text); text = "" }
                        }
                        FlatButton {
                            label: "Add"
                            active: hostAddField.text.length > 0
                            onClicked: { controller.addPeerByHost(hostAddField.text); hostAddField.text = "" }
                        }
                    }
                    Text {
                        text: controller.rsyncSummary
                        color: controller.usingOpenRsync ? Theme.warning : Theme.textTertiary
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.border }

            // main panel (black)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        text: "Untitled sync"
                        placeholderText: qsTr("Job name")
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textTertiary
                        font.pixelSize: 16
                        background: Rectangle {
                            color: "transparent"
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: nameField.activeFocus ? Theme.accent : Theme.border
                            }
                        }
                    }
                    FlatButton {
                        label: "Save job"
                        onClicked: controller.saveJob(root.jobMap())
                    }
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 8
                    Layout.fillWidth: true

                    Text { text: "From"; color: Theme.textSecondary; font.pixelSize: 12 }
                    Field {
                        id: fromField
                        Layout.fillWidth: true
                        placeholderText: qsTr("local path, user@host:/path, or rsync://host/module")
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Tab) { root.completePath(fromField); event.accepted = true }
                        }
                    }

                    Text { text: "To"; color: Theme.textSecondary; font.pixelSize: 12 }
                    Field {
                        id: toField
                        Layout.fillWidth: true
                        placeholderText: qsTr("local path, user@host:/path, or rsync://host/module")
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Tab) { root.completePath(toField); event.accepted = true }
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
                    visible: root.deleteOn
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "limit deletions to"; color: Theme.textSecondary; font.pixelSize: 12 }
                    Field {
                        id: maxDeleteField
                        Layout.preferredWidth: 80
                        placeholderText: "0 = no limit"
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 0; top: 999999 }
                    }
                    Text { text: "files — aborts if a sync would delete more"; color: Theme.textTertiary; font.pixelSize: 11 }
                }

                // Connection options — appear only when an endpoint is remote.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: root.showSsh || root.showDaemon

                    RowLayout {
                        visible: root.showSsh
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: "SSH key"; color: Theme.textSecondary; font.pixelSize: 12 }
                        Field {
                            id: sshKeyField
                            Layout.fillWidth: true
                            placeholderText: qsTr("~/.ssh/id_ed25519 — optional, uses agent/default if blank")
                        }
                        Text { text: "Port"; color: Theme.textSecondary; font.pixelSize: 12 }
                        Field {
                            id: sshPortField
                            Layout.preferredWidth: 64
                            placeholderText: "22"
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 65535 }
                        }
                    }
                    RowLayout {
                        visible: root.showDaemon
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: "Password"; color: Theme.textSecondary; font.pixelSize: 12 }
                        Field {
                            id: daemonPwField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            placeholderText: qsTr("rsync daemon password (kept this session, not saved)")
                        }
                    }
                    Text {
                        visible: root.showSsh
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Over SSH · agent keys honored · host trusted on first use, changed keys rejected"
                        color: Theme.textTertiary
                        font.pixelSize: 10
                    }
                }

                // Schedule — registers an OS timer (launchd/systemd) unless Manual.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "SCHEDULE"; color: Theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1; Layout.rightMargin: 4 }
                        Chip { label: "manual"; active: root.scheduleKind === "manual"; onToggled: root.scheduleKind = "manual" }
                        Chip { label: "interval"; active: root.scheduleKind === "interval"; onToggled: root.scheduleKind = "interval" }
                        Chip { label: "daily"; active: root.scheduleKind === "daily"; onToggled: root.scheduleKind = "daily" }
                        Chip { label: "weekly"; active: root.scheduleKind === "weekly"; onToggled: root.scheduleKind = "weekly" }
                    }

                    RowLayout {
                        visible: root.scheduleKind === "interval"
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: "every"; color: Theme.textSecondary; font.pixelSize: 12 }
                        Field {
                            id: intervalField
                            Layout.preferredWidth: 70
                            text: "60"
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 525600 }
                        }
                        Text { text: "minutes"; color: Theme.textSecondary; font.pixelSize: 12 }
                    }

                    RowLayout {
                        visible: root.scheduleKind === "daily" || root.scheduleKind === "weekly"
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: "at"; color: Theme.textSecondary; font.pixelSize: 12 }
                        Field {
                            id: timeField
                            Layout.preferredWidth: 80
                            text: "09:00"
                            placeholderText: "HH:MM"
                            validator: RegularExpressionValidator {
                                regularExpression: /^([01][0-9]|2[0-3]):[0-5][0-9]$/
                            }
                        }
                    }

                    RowLayout {
                        visible: root.scheduleKind === "weekly"
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "on"; color: Theme.textSecondary; font.pixelSize: 12 }
                        Repeater {
                            model: ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"]
                            Chip { label: modelData; active: root.weekday === index; onToggled: root.weekday = index }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    readonly property bool canStart: !controller.running && fromField.text.length > 0 && toField.text.length > 0
                    FlatButton {
                        label: "Preview"
                        active: parent.canStart
                        onClicked: controller.preview(root.jobMap())
                    }
                    FlatButton {
                        label: controller.running ? "Running…" : "Run sync"
                        primary: true
                        active: parent.canStart
                        onClicked: {
                            if (root.deleteOn)
                                root.confirmOpen = true
                            else
                                controller.run(root.jobMap())
                        }
                    }
                    Item { Layout.fillWidth: true }
                    FlatButton { label: "Cancel"; active: controller.running; onClicked: controller.cancel() }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "PREVIEW"; color: Theme.textTertiary; font.pixelSize: 11; font.letterSpacing: 1 }
                    Text { text: controller.changes.count + " changes"; color: Theme.textSecondary; font.pixelSize: 12 }
                    Text {
                        visible: controller.changes.deletions > 0
                        text: "· " + controller.changes.deletions + " to delete"
                        color: Theme.danger
                        font.pixelSize: 12
                    }
                }

                // Drag the handle to resize the preview vs. the log.
                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Vertical

                    handle: Rectangle {
                        implicitHeight: 9
                        color: "transparent"
                        Rectangle {
                            anchors.centerIn: parent
                            width: 40
                            height: 3
                            radius: 1.5
                            color: SplitHandle.pressed ? Theme.accent
                                                       : (SplitHandle.hovered ? Theme.textTertiary : Theme.border)
                        }
                    }

                    // Preview (change list)
                    Rectangle {
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 80
                        radius: Theme.radius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.border
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
                                        color: isDelete ? Theme.danger : (isNew ? Theme.ok : Theme.info)
                                        font.family: Theme.mono
                                        font.pixelSize: 12
                                        Layout.preferredWidth: 96
                                    }
                                    Text {
                                        text: path
                                        color: Theme.textPrimary
                                        font.family: Theme.mono
                                        font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }

                    // Log
                    Rectangle {
                        SplitView.minimumHeight: 56
                        SplitView.preferredHeight: 180
                        radius: Theme.radius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 2
                            Text { text: "LOG"; color: Theme.textTertiary; font.pixelSize: 10; font.letterSpacing: 1; Layout.leftMargin: 4 }
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                TextArea {
                                    readOnly: true
                                    text: controller.log
                                    color: Theme.textTertiary
                                    font.family: Theme.mono
                                    font.pixelSize: 11
                                    wrapMode: TextEdit.NoWrap
                                    background: Rectangle { color: "transparent" }
                                }
                            }
                        }
                    }
                }

                // Progress / status footer
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 5
                        radius: 3
                        color: Theme.bgTertiary
                        Rectangle {
                            height: parent.height
                            radius: 3
                            width: parent.width * Math.max(0, Math.min(100, controller.percent)) / 100
                            color: Theme.accent
                        }
                    }
                    Text { text: controller.status; color: Theme.textSecondary; font.pixelSize: 12 }
                }
            }
        }
    }

    DeleteConfirmDialog {
        open: root.confirmOpen
        deletions: controller.changes.deletions
        fromText: fromField.text
        toText: toField.text
        onCanceled: root.confirmOpen = false
        onConfirmed: {
            root.confirmOpen = false
            controller.run(root.jobMap())
        }
    }
}
