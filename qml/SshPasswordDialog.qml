import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shown when an SSH sync fails public-key auth (controller.sshAuthRequired). Collects
// a username/password to retry with; the password is fed to ssh via SSH_ASKPASS and,
// if "remember" is set, stored in the OS keychain and attached to the saved SSH
// host. Modeled on DeleteConfirmDialog: a full-screen scrim with a centered panel.
Rectangle {
    id: root

    property bool open: false
    property string host
    property bool remember: false

    signal canceled()
    signal submitted(string user, string password, bool remember)

    // Open the dialog prefilled with a username parsed from the endpoint (may be blank).
    function show(hostText, userText) {
        host = hostText
        userField.text = userText
        pwField.text = ""
        remember = false
        open = true
        pwField.forceActiveFocus()
    }

    function accept() {
        if (pwField.text.length === 0)
            return
        root.submitted(userField.text.trim(), pwField.text, root.remember)
        root.open = false
    }

    anchors.fill: parent
    visible: open
    color: "#cc000000"
    focus: open
    Keys.onEscapePressed: root.canceled()

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

            RowLayout {
                spacing: 8
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.accent
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    text: "Password sign-in"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: "Key authentication failed for " + root.host
                      + ". Enter a username and password to connect."
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "User"; color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 64 }
                Field {
                    id: userField
                    Layout.fillWidth: true
                    placeholderText: qsTr("username")
                    onAccepted: pwField.forceActiveFocus()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Password"; color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 64 }
                Field {
                    id: pwField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: qsTr("password")
                    onAccepted: root.accept()
                }
            }

            Chip {
                label: "remember on this machine (keychain)"
                active: root.remember
                onToggled: root.remember = !root.remember
                tooltip: "Stores the password in the OS keychain so this host can reuse it later"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                FlatButton { label: "Cancel"; onClicked: root.canceled() }
                FlatButton {
                    label: "Authenticate & retry"
                    onClicked: root.accept()
                }
            }
        }
    }
}
