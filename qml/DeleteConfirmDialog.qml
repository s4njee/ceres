import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property bool open: false
    property int deletions: 0
    property string fromText
    property string toText

    signal canceled()
    signal confirmed()

    anchors.fill: parent
    visible: open
    color: "#cc000000"

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
                    color: Theme.danger
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    text: "Delete extras is on"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: (root.deletions > 0
                       ? root.deletions + " file(s) in the destination will be permanently deleted."
                       : "Files in the destination that aren't in the source will be permanently deleted.")
                      + " This cannot be undone - preview first to see exactly which."
            }

            Text {
                Layout.fillWidth: true
                color: Theme.textTertiary
                font.family: Theme.mono
                font.pixelSize: 11
                elide: Text.ElideMiddle
                text: root.fromText + "  ->  " + root.toText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                FlatButton { label: "Cancel"; onClicked: root.canceled() }
                FlatButton {
                    label: "Sync and delete"
                    danger: true
                    onClicked: root.confirmed()
                }
            }
        }
    }
}
