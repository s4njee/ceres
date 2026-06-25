import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Modal to add a saved SSH host directly (not tied to a sync job). Records
// non-secret target metadata only — host, optional key path and port. A password
// is captured later by the auth modal the first time the host is used.
Rectangle {
    id: root

    property bool open: false

    function show() {
        hostField.text = ""
        labelField.text = ""
        keyField.text = ""
        portField.text = ""
        open = true
        hostField.forceActiveFocus()
    }

    function save() {
        var t = hostField.text.trim()
        if (t.length === 0)
            return
        // saveSshHostForJob parses an SSH endpoint out of the job's destination.
        // Use a home-dir path so bare hosts like "server" are not mistaken for
        // ambiguous local "label:" paths by EndpointParser.
        if (t.indexOf(":") < 0)
            t += ":~/"
        else if (t.endsWith(":"))
            t += "~/"
        controller.saveSshHostForJob({
            destination: t,
            name: labelField.text.trim(),   // optional friendly label (empty = use target)
            sshKey: keyField.text.trim(),
            sshPort: parseInt(portField.text) || 0
        })
        open = false
    }

    anchors.fill: parent
    visible: open
    color: "#cc000000"
    focus: open
    Keys.onEscapePressed: root.open = false

    MouseArea { anchors.fill: parent }

    Rectangle {
        anchors.centerIn: parent
        width: 400
        height: panelCol.implicitHeight + 36
        radius: Theme.radius
        color: Theme.bgSecondary
        border.width: 1
        border.color: Theme.borderStrong

        ColumnLayout {
            id: panelCol
            x: 18
            y: 18
            width: parent.width - 36
            spacing: 12

            Text {
                text: "New SSH host"
                color: Theme.textPrimary
                font.pixelSize: 15
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: "Save a host to the sidebar. You'll be asked for a password the first time you connect."
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "Host"; color: Theme.textSecondary; font.pixelSize: 11 }
                Field {
                    id: hostField
                    Layout.fillWidth: true
                    placeholderText: "user@host"
                    onAccepted: root.save()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "Label (optional)"; color: Theme.textSecondary; font.pixelSize: 11 }
                Field {
                    id: labelField
                    Layout.fillWidth: true
                    placeholderText: "Friendly name for the sidebar"
                    onAccepted: root.save()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "SSH key (optional)"; color: Theme.textSecondary; font.pixelSize: 11 }
                    Field {
                        id: keyField
                        Layout.fillWidth: true
                        placeholderText: "~/.ssh/id_ed25519"
                        onAccepted: root.save()
                    }
                }
                ColumnLayout {
                    spacing: 4
                    Text { text: "Port"; color: Theme.textSecondary; font.pixelSize: 11 }
                    Field {
                        id: portField
                        Layout.preferredWidth: 64
                        placeholderText: "22"
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 0; top: 65535 }
                        onAccepted: root.save()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                FlatButton {
                    label: "Cancel"
                    onClicked: root.open = false
                }
                FlatButton {
                    label: "Add host"
                    primary: true
                    active: hostField.text.trim().length > 0
                    onClicked: root.save()
                }
            }
        }
    }
}
