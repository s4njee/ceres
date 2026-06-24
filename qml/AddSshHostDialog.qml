import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Prompts before an unknown SSH target is used by a new sync. Saving the host
// records non-secret target metadata only; passwords are saved from the auth modal.
Rectangle {
    id: root

    property bool open: false
    property string target: ""

    signal accepted()
    signal skipped()

    function show(targetText) {
        target = targetText
        open = true
    }

    anchors.fill: parent
    visible: open
    color: "#cc000000"
    focus: open
    Keys.onEscapePressed: {
        root.open = false
        root.skipped()
    }

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
                text: "Save SSH host?"
                color: Theme.textPrimary
                font.pixelSize: 15
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: "Add " + root.target + " to the saved SSH hosts sidebar?"
            }

            Text {
                Layout.fillWidth: true
                color: Theme.textTertiary
                font.family: Theme.mono
                font.pixelSize: 11
                elide: Text.ElideMiddle
                text: root.target
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                FlatButton {
                    label: "Not now"
                    onClicked: {
                        root.open = false
                        root.skipped()
                    }
                }
                FlatButton {
                    label: "Add host"
                    onClicked: {
                        root.open = false
                        root.accepted()
                    }
                }
            }
        }
    }
}
