import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property bool open: false
    property string host: ""

    signal canceled()
    signal confirmed()

    function show(hostText) {
        host = hostText
        open = true
    }

    anchors.fill: parent
    visible: open
    color: "#cc000000"
    focus: open
    Keys.onEscapePressed: root.canceled()

    MouseArea { anchors.fill: parent }

    Rectangle {
        anchors.centerIn: parent
        width: 430
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
                    color: Theme.warning
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    text: "Host key changed"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: "SSH says the saved host key for " + root.host
                      + " no longer matches. If you reinstalled this machine and trust the new OS, remove the old known_hosts entry and retry."
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.warning
                font.pixelSize: 11
                text: "Only continue if you expected this host key to change."
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                FlatButton { label: "Cancel"; onClicked: root.canceled() }
                FlatButton {
                    label: "Remove old key and retry"
                    danger: true
                    onClicked: root.confirmed()
                }
            }
        }
    }
}
