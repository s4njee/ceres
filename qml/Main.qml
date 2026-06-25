// Main.qml — The top-level application window and primary UI for Ceres.
//
// Layout structure (from outer to inner):
//   ┌─────────────────────────────────────────────┐
//   │ Top strip: app name + host IP pill           │
//   ├──────────┬──────────────────────────────────┤
//   │ Sidebar  │ Main editor panel                │
//   │ (210px)  │  - From/To path fields + Browse  │
//   │          │  - Option chips (archive, etc.)  │
//   │ SSH      │  - Preview / Run / Cancel buttons │
//   │ hosts    │  - SplitView: preview + log      │
//   │ NETWORK  │  - Progress bar + status          │
//   │ peers    │                                   │
//   └──────────┴──────────────────────────────────┘
//
// Data flows through the `controller` context property (a C++ JobController).
// The QML `jobMap()` function gathers the editor's current state into a
// QVariantMap and passes it to controller.preview() / controller.run().
// Controller signals (onRemoteCompleted, onSshAuthRequired) push state back
// into the editor.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1000
    height: 680
    minimumWidth: 820
    minimumHeight: 520
    visible: true
    title: qsTr("Ceres")
    color: Theme.bgPrimary

    property bool archiveOn: true
    property bool compressOn: false
    property bool deleteOn: false
    property bool checksumOn: false
    property bool confirmOpen: false
    property int currentTab: 1   // 0 = Sync editor, 1 = Browse

    // SSH password auth state. Modal-driven (no always-visible field): held for the
    // session so subsequent manual runs reuse it; persisted to the keychain only when
    // the user ticks "remember" in the auth modal (see retryWithPassword).
    property string sshPassword: ""
    property bool rememberSshPassword: false
    // Which flow opened the auth modal: "run" (a sync) or "browse" (remote folder picker).
    property string authContext: "run"
    property string authBrowseInput: ""
    property string pendingHostAction: ""

    // Advanced options (supported in backend/argv/fingerprint/storage but not yet
    // exposed in the form UI; round-tripped here so loads/previews/runs/saves
    // and the --delete safety gate preserve them for jobs that have them).
    property var excludes: []
    property var extraArgs: []
    readonly property int completionChoiceLimit: 12
    readonly property int remoteBrowseLimit: 200

    function endpointKind(p) { return controller.endpointKind(p || "") }
    function looksDaemon(p) { return endpointKind(p) === "daemon" }
    function looksRemote(p) {
        if (!p) return false
        var kind = endpointKind(p)
        return kind === "ssh" || kind === "daemon"
    }
    function looksSsh(p) { return endpointKind(p) === "ssh" }

    function setPathField(field, path) {
        field.text = path
        field.cursorPosition = path.length
    }

    // Turn a FolderDialog/FileDialog "file://" URL into a native path. On Windows the
    // URL is file:///C:/..., so stripping the scheme leaves "/C:/..." — drop that
    // leading slash before the drive letter so rsync gets a real "C:/..." path.
    function fileUrlToPath(url) {
        var p = ("" + url).replace(/^file:\/\//, "")
        if (/^\/[A-Za-z]:/.test(p)) p = p.substring(1)
        return decodeURIComponent(p)
    }

    function toggleSavedHostTarget(target) {
        var endpoint = target + ":~/"
        if (toField.text === endpoint) {
            root.setPathField(toField, "")
            root.setPathField(fromField, endpoint)
            fromField.forceActiveFocus()
        } else if (fromField.text === endpoint) {
            root.setPathField(fromField, "")
            root.setPathField(toField, endpoint)
            toField.forceActiveFocus()
        } else {
            root.setPathField(toField, endpoint)
            toField.forceActiveFocus()
        }
    }

    // Inject/replace the login user in an SSH endpoint string ("host:/p" ->
    // "user@host:/p"). Mirrors EndpointParser::withUser; no-op for blank user or
    // non-SSH text.
    function sshWithUser(text, user) {
        if (!user || user.length === 0 || !looksSsh(text))
            return text
        var colon = text.indexOf(":")
        if (colon < 0) return text
        var target = text.substring(0, colon)
        var rest = text.substring(colon)  // includes ":"
        var at = target.lastIndexOf("@")
        if (at >= 0) target = target.substring(at + 1)
        return user + "@" + target + rest
    }

    // Apply sshWithUser to a path field so the editor matches the credentials used
    // for a password retry.
    function applyUserToSshField(field, user) {
        var t = root.sshWithUser(field.text, user)
        if (t !== field.text)
            root.setPathField(field, t)
    }

    function showCompletionChoices(field, choices) {
        if (choices && choices.length > 1)
            completionPopup.showFor(field, choices)
        else if (completionPopup.targetField === field)
            completionPopup.close()
    }

    function browsePath(field) {
        if (looksSsh(field.text)) {
            remoteBrowser.showFor(field)
        } else if (!looksDaemon(field.text)) {
            if (field === fromField)
                fromFolderDialog.open()
            else
                toFolderDialog.open()
        }
    }

    function handleCompletionKey(field, event) {
        if (completionPopup.visible && completionPopup.targetField === field) {
            if (event.key === Qt.Key_Down) {
                completionPopup.moveSelection(1)
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Up) {
                completionPopup.moveSelection(-1)
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab) {
                completionPopup.acceptSelection()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Escape) {
                completionPopup.close()
                event.accepted = true
                return
            }
        }

        if (event.key === Qt.Key_Tab) {
            root.completePath(field)
            event.accepted = true
        }
    }

    // Tab-complete a path field: local paths complete synchronously; remote
    // (user@host:) targets complete over ssh and arrive via onRemoteCompleted.
    function completePath(field) {
        var t = field.text
        if (t.length === 0 || looksDaemon(t)) return
        if (looksSsh(t)) {
            completer.completeRemote(t, sshKeyField.text, parseInt(sshPortField.text) || 0,
                                     root.completionChoiceLimit, root.sshPassword)
        } else {
            var choices = completer.localChoices(t, root.completionChoiceLimit)
            var c = completer.completeLocal(t)
            if (c.length > 0 && c !== t)
                root.setPathField(field, c)
            root.showCompletionChoices(field, choices)
        }
    }

    readonly property bool showSsh: looksSsh(fromField.text) || looksSsh(toField.text)
    readonly property bool showDaemon: looksDaemon(fromField.text) || looksDaemon(toField.text)

    function jobMap() {
        return {
            source: fromField.text,
            destination: toField.text,
            archive: root.archiveOn,
            compress: root.compressOn,
            deleteExtras: root.deleteOn,
            checksum: root.checksumOn,
            maxDelete: parseInt(maxDeleteField.text) || 0,
            excludes: root.excludes,
            extraArgs: root.extraArgs,
            sshKey: sshKeyField.text,
            sshPort: parseInt(sshPortField.text) || 0,
            daemonPassword: daemonPwField.text,
            sshPassword: root.sshPassword,
            rememberSshPassword: root.rememberSshPassword
        }
    }

    function unknownSshTarget() {
        var target = controller.sshTargetForJob(root.jobMap())
        if (target && target.length > 0 && !controller.isSshHostSaved(target))
            return target
        return ""
    }

    function continueHostAction(action) {
        if (action === "preview") {
            if (!controller.running && fromField.text.length > 0 && toField.text.length > 0)
                controller.preview(root.jobMap())
        } else if (action === "run") {
            if (!controller.running && fromField.text.length > 0 && toField.text.length > 0) {
                if (root.deleteOn)
                    root.confirmOpen = true
                else
                    controller.run(root.jobMap())
            }
        }
    }

    function runWithHostPrompt(action) {
        var target = root.unknownSshTarget()
        if (target.length > 0) {
            root.pendingHostAction = action
            addSshHostDialog.show(target)
            return
        }
        root.continueHostAction(action)
    }

    // Basic keyboard shortcuts (Ctrl maps to Cmd on macOS).
    Shortcut {
        sequence: "Ctrl+Return"
        onActivated: {
            if (!controller.running && fromField.text.length > 0 && toField.text.length > 0)
                root.runWithHostPrompt("preview")
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+Return"
        onActivated: {
            if (!controller.running && fromField.text.length > 0 && toField.text.length > 0) {
                root.runWithHostPrompt("run")
            }
        }
    }

    Connections {
        target: controller
        function onSshAuthRequired(host, user) {
            root.authContext = "run"
            sshAuthDialog.show(host, user)
        }
        function onSshHostKeyChanged(host) {
            knownHostDialog.show(host)
        }
    }

    Connections {
        target: completer
        function onRemoteCompleted(input, completion, choices) {
            var field = null
            if (fromField.text === input)
                field = fromField
            else if (toField.text === input)
                field = toField
            if (!field)
                return

            if (completion.length > 0 && completion !== input)
                root.setPathField(field, completion)
            root.showCompletionChoices(field, choices || [])
        }

        function onRemoteBrowseCompleted(input, current, directories, error) {
            remoteBrowser.complete(input, current, directories || [], error || "")
        }

        // Browse hit a key-auth failure: close the folder picker (a Popup renders above
        // the modal) and prompt for a password; the retry reopens it (see onSubmitted).
        function onRemoteAuthRequired(input, host, user) {
            root.authContext = "browse"
            root.authBrowseInput = input
            remoteBrowser.close()
            sshAuthDialog.show(host, user)
        }
    }

    FolderDialog {
        id: fromFolderDialog
        title: "Select Source Folder"
        onAccepted: {
            var p = root.fileUrlToPath(selectedFolder);
            fromField.text = p;
            fromField.cursorPosition = fromField.text.length;
        }
    }

    FolderDialog {
        id: toFolderDialog
        title: "Select Destination Folder"
        onAccepted: {
            var p = root.fileUrlToPath(selectedFolder);
            toField.text = p;
            toField.cursorPosition = toField.text.length;
        }
    }

    Popup {
        id: completionPopup

        property var targetField: null
        property int selectedIndex: 0

        function showFor(field, choices) {
            targetField = field
            selectedIndex = 0
            completionModel.clear()
            for (var i = 0; i < choices.length; ++i)
                completionModel.append({ value: choices[i] })

            width = Math.max(220, field.width)
            var p = field.mapToItem(root.contentItem, 0, field.height + 2)
            x = Math.max(0, Math.min(root.width - width, p.x))
            y = Math.max(0, Math.min(root.height - implicitHeight - 8, p.y))
            open()
        }

        function moveSelection(delta) {
            if (completionModel.count === 0)
                return
            selectedIndex = Math.max(0, Math.min(completionModel.count - 1, selectedIndex + delta))
            completionList.positionViewAtIndex(selectedIndex, ListView.Contain)
        }

        function acceptSelection() {
            if (!targetField || completionModel.count === 0)
                return
            var value = completionModel.get(selectedIndex).value
            root.setPathField(targetField, value)
            close()
            targetField.forceActiveFocus()
        }

        padding: 0
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        implicitHeight: Math.min(completionList.contentHeight, 180)
        background: Rectangle {
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderStrong
            radius: Theme.radius
        }

        ListModel { id: completionModel }

        contentItem: ListView {
            id: completionList
            width: completionPopup.width
            implicitHeight: Math.min(contentHeight, 180)
            clip: true
            model: completionModel
            currentIndex: completionPopup.selectedIndex

            delegate: Rectangle {
                required property string value
                required property int index

                width: completionList.width
                height: 28
                color: index === completionPopup.selectedIndex ? Theme.bgTertiary : "transparent"

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    text: value
                    color: Theme.textPrimary
                    font.family: Theme.mono
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: completionPopup.selectedIndex = index
                    onClicked: completionPopup.acceptSelection()
                }
            }
        }
    }

    Popup {
        id: remoteBrowser

        property var targetField: null
        property string requestInput: ""
        property string currentPath: ""
        property string message: ""
        property bool loading: false
        property int selectedIndex: -1

        function showFor(field) {
            targetField = field
            open()
            load(field.text)
        }

        function load(path) {
            requestInput = path
            currentPath = path
            message = "Loading..."
            loading = true
            selectedIndex = -1
            remoteDirModel.clear()
            completer.browseRemote(path, sshKeyField.text, parseInt(sshPortField.text) || 0,
                                   root.remoteBrowseLimit, root.sshPassword)
        }

        function complete(input, current, directories, error) {
            if (!visible || input !== requestInput)
                return
            loading = false
            currentPath = current.length > 0 ? current : input
            remoteDirModel.clear()
            for (var i = 0; i < directories.length; ++i)
                remoteDirModel.append({ value: directories[i], label: displayName(directories[i]) })
            // Start with nothing selected so Choose picks the folder you're viewing;
            // click (or arrow keys) to explicitly select a child instead.
            selectedIndex = -1
            message = error.length > 0 ? error : (remoteDirModel.count === 0 ? "No folders" : "")
            remoteDirList.forceActiveFocus()
        }

        function displayName(path) {
            var s = path
            if (s.endsWith("/"))
                s = s.substring(0, s.length - 1)
            var slash = s.lastIndexOf("/")
            return (slash >= 0 ? s.substring(slash + 1) : s) + "/"
        }

        function selectedPath() {
            if (selectedIndex < 0 || selectedIndex >= remoteDirModel.count)
                return currentPath
            return remoteDirModel.get(selectedIndex).value
        }

        function chooseSelectedOrCurrent() {
            if (!targetField)
                return
            root.setPathField(targetField, selectedPath())
            close()
            targetField.forceActiveFocus()
        }

        function openSelected() {
            if (selectedIndex < 0 || selectedIndex >= remoteDirModel.count)
                return
            load(remoteDirModel.get(selectedIndex).value)
        }

        function parentPath(path) {
            var colon = path.indexOf(":")
            if (colon < 0)
                return path
            var target = path.substring(0, colon + 1)
            var p = path.substring(colon + 1)
            if (p.length === 0 || p === "/")
                return path
            if (p.endsWith("/") && p.length > 1)
                p = p.substring(0, p.length - 1)
            var slash = p.lastIndexOf("/")
            if (slash <= 0)
                return target + "/"
            return target + p.substring(0, slash + 1)
        }

        function moveSelection(delta) {
            if (remoteDirModel.count === 0)
                return
            selectedIndex = Math.max(0, Math.min(remoteDirModel.count - 1, selectedIndex + delta))
            remoteDirList.positionViewAtIndex(selectedIndex, ListView.Contain)
        }

        width: Math.min(root.width - 48, 560)
        height: Math.min(root.height - 64, 430)
        x: Math.max(24, (root.width - width) / 2)
        y: Math.max(24, (root.height - height) / 2)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 0
        background: Rectangle {
            color: Theme.bgSecondary
            border.width: 1
            border.color: Theme.borderStrong
            radius: Theme.radius
        }

        ListModel { id: remoteDirModel }

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.topMargin: 12
                text: "Remote folder"
                color: Theme.textPrimary
                font.pixelSize: 15
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                text: remoteBrowser.currentPath
                color: Theme.textTertiary
                font.family: Theme.mono
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                spacing: 8

                FlatButton {
                    label: "Up"
                    active: !remoteBrowser.loading
                    onClicked: remoteBrowser.load(remoteBrowser.parentPath(remoteBrowser.currentPath))
                }
                Text {
                    Layout.fillWidth: true
                    text: remoteBrowser.message
                    color: Theme.textTertiary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                color: Theme.bgPrimary
                radius: Theme.radius
                border.width: 1
                border.color: Theme.border

                ListView {
                    id: remoteDirList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: remoteDirModel
                    currentIndex: remoteBrowser.selectedIndex
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Down) {
                            remoteBrowser.moveSelection(1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up) {
                            remoteBrowser.moveSelection(-1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            remoteBrowser.openSelected()
                            event.accepted = true
                        }
                    }

                    delegate: Rectangle {
                        id: dirRow
                        required property string value
                        required property string label
                        required property int index

                        readonly property bool selected: index === remoteBrowser.selectedIndex

                        width: remoteDirList.width
                        height: 28
                        radius: Theme.radius
                        // Selection (click/keyboard) is the strong highlight; hover is a
                        // subtle, separate cue so it never decides what Choose picks.
                        color: selected ? Theme.bgTertiary
                                        : (dirMouse.containsMouse ? Theme.bgSecondary : "transparent")
                        border.width: selected ? 1 : 0
                        border.color: Theme.accent

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: Text.AlignVCenter
                            text: label
                            color: Theme.textPrimary
                            font.family: Theme.mono
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }

                        MouseArea {
                            id: dirMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: remoteBrowser.selectedIndex = index
                            onDoubleClicked: remoteBrowser.openSelected()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.bottomMargin: 12
                spacing: 8

                Item { Layout.fillWidth: true }
                FlatButton { label: "Cancel"; onClicked: remoteBrowser.close() }
                FlatButton {
                    label: "Open"
                    active: remoteBrowser.selectedIndex >= 0 && !remoteBrowser.loading
                    onClicked: remoteBrowser.openSelected()
                }
                FlatButton {
                    label: remoteBrowser.selectedIndex >= 0 ? "Choose selected" : "Choose this folder"
                    active: !remoteBrowser.loading
                    onClicked: remoteBrowser.chooseSelectedOrCurrent()
                }
            }
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
                Item { width: 8 }
                Repeater {
                    model: ["Sync", "Browse"]
                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        radius: Theme.radius
                        color: root.currentTab === index ? Theme.bgTertiary : "transparent"
                        implicitHeight: 24
                        implicitWidth: tabLbl.implicitWidth + 20
                        Text {
                            id: tabLbl
                            anchors.centerIn: parent
                            text: modelData
                            color: root.currentTab === index ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: 12
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.currentTab = index }
                    }
                }
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

        // ---------- body (tabbed: Sync | Browse) ----------
        StackLayout {
            id: bodyStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentTab

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
                            width: ListView.view.width
                            height: 42
                            radius: Theme.radius
                            color: hostMouse.containsMouse ? Theme.bgTertiary : "transparent"

                            MouseArea {
                                id: hostMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleSavedHostTarget(target)
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8
                                Rectangle { width: 7; height: 7; radius: 4; color: Theme.ok; Layout.alignment: Qt.AlignVCenter }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: target; color: Theme.textPrimary; font.family: Theme.mono; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: summary; color: Theme.textTertiary; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                }
                            }
                        }
                    }

                    FlatButton { Layout.fillWidth: true; label: "+  New host"; onClicked: newHostDialog.show() }

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
                            text: "Ø  No machines found"
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
                                    toField.text = daemon ? ("rsync://" + address + "/") : (address + ":~/")
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

                GridLayout {
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 8
                    Layout.fillWidth: true

                    Text { text: "From"; color: Theme.textSecondary; font.pixelSize: 12 }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Field {
                            id: fromField
                            Layout.fillWidth: true
                            placeholderText: qsTr("local path, user@host:/path, or rsync://host/module")
                            Keys.onPressed: function(event) {
                                root.handleCompletionKey(fromField, event)
                            }
                            onTextEdited: completionPopup.close()
                        }
                        FlatButton {
                            label: "Browse"
                            visible: !looksDaemon(fromField.text)
                            onClicked: root.browsePath(fromField)
                        }
                    }

                    Text { text: "To"; color: Theme.textSecondary; font.pixelSize: 12 }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Field {
                            id: toField
                            Layout.fillWidth: true
                            placeholderText: qsTr("local path, user@host:/path, or rsync://host/module")
                            Keys.onPressed: function(event) {
                                root.handleCompletionKey(toField, event)
                            }
                            onTextEdited: completionPopup.close()
                        }
                        FlatButton {
                            label: "Browse"
                            visible: !looksDaemon(toField.text)
                            onClicked: root.browsePath(toField)
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Chip { label: "archive"; active: root.archiveOn; onToggled: root.archiveOn = !root.archiveOn; tooltip: "Preserves permissions, times, and symbolic links (-a)" }
                    Chip { label: "compress"; active: root.compressOn; onToggled: root.compressOn = !root.compressOn; tooltip: "Compresses file data during the transfer (-z)" }
                    Chip { label: "delete extras"; warn: true; active: root.deleteOn; onToggled: root.deleteOn = !root.deleteOn; tooltip: "Deletes files in destination not present in source (--delete)" }
                    Chip { label: "checksum"; active: root.checksumOn; onToggled: root.checksumOn = !root.checksumOn; tooltip: "Skips files based on strict checksums rather than mod-time (-c)" }
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
                            validator: IntValidator { bottom: 0; top: 65535 }
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    readonly property bool canStart: !controller.running && fromField.text.length > 0 && toField.text.length > 0
                    FlatButton {
                        label: "Preview"
                        active: parent.canStart
                        onClicked: root.runWithHostPrompt("preview")
                    }
                    FlatButton {
                        label: controller.running ? "Running…" : "Run sync"
                        primary: true
                        active: parent.canStart
                        onClicked: root.runWithHostPrompt("run")
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
                    Text {
                        visible: controller.running && controller.speed.length > 0
                        text: controller.speed
                        color: Theme.textSecondary
                        font.family: Theme.mono
                        font.pixelSize: 12
                    }
                    Text {
                        visible: controller.running && controller.bytesProgress.length > 0
                        text: controller.bytesProgress
                        color: Theme.textSecondary
                        font.family: Theme.mono
                        font.pixelSize: 12
                    }
                    Text { text: controller.status; color: Theme.textSecondary; font.pixelSize: 12 }
                }
            }
        }
        // Browse tab (page 1)
        BrowseTab {
            Layout.fillWidth: true
            Layout.fillHeight: true
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

    SshPasswordDialog {
        id: sshAuthDialog
        onCanceled: open = false
        onSubmitted: function(user, password, remember) {
            root.sshPassword = password
            root.rememberSshPassword = remember
            if (root.authContext === "browse") {
                if (remember) {
                    controller.saveSshHostPassword(root.authBrowseInput, user, password,
                                                   sshKeyField.text,
                                                   parseInt(sshPortField.text) || 0)
                }
                // Reopen the folder picker and re-list with the new credentials. The
                // username is folded into the browse target so chosen paths carry it.
                remoteBrowser.open()
                remoteBrowser.load(root.sshWithUser(root.authBrowseInput, user))
            } else {
                root.applyUserToSshField(fromField, user)
                root.applyUserToSshField(toField, user)
                controller.retryWithPassword(root.jobMap(), user, password, remember)
            }
        }
    }

    KnownHostChangedDialog {
        id: knownHostDialog
        onCanceled: open = false
        onConfirmed: {
            open = false
            controller.repairKnownHostAndRetry(root.jobMap())
        }
    }

    AddSshHostDialog {
        id: addSshHostDialog
        onAccepted: {
            controller.saveSshHostForJob(root.jobMap())
            var action = root.pendingHostAction
            root.pendingHostAction = ""
            root.continueHostAction(action)
        }
        onSkipped: {
            var action = root.pendingHostAction
            root.pendingHostAction = ""
            root.continueHostAction(action)
        }
    }

    NewHostDialog { id: newHostDialog }
}
