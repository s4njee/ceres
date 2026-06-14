import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 920
    height: 660
    visible: true
    title: qsTr("Ceres — rsync")

    Material.theme: Material.Dark
    Material.accent: Material.Teal

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            Label {
                text: "Ceres"
                font.pixelSize: 20
                font.bold: true
                Layout.alignment: Qt.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            Label {
                text: controller.rsyncSummary
                color: controller.usingOpenRsync ? Material.color(Material.Amber) : "#b0b0b0"
                font.pixelSize: 12
                elide: Text.ElideMiddle
                Layout.maximumWidth: 560
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        GridLayout {
            columns: 3
            columnSpacing: 8
            rowSpacing: 8
            Layout.fillWidth: true

            Label { text: qsTr("From") }
            TextField {
                id: srcField
                Layout.fillWidth: true
                placeholderText: qsTr("source path (a trailing / copies contents, e.g. ./a/)")
            }
            Button {
                text: controller.running ? qsTr("Running…") : qsTr("Preview (dry-run)")
                enabled: !controller.running && srcField.text.length > 0 && dstField.text.length > 0
                onClicked: controller.preview(srcField.text, dstField.text)
            }

            Label { text: qsTr("To") }
            TextField {
                id: dstField
                Layout.fillWidth: true
                placeholderText: qsTr("destination path (e.g. ./b/)")
            }
            Button {
                text: qsTr("Cancel")
                enabled: controller.running
                onClicked: controller.cancel()
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: controller.percent
            indeterminate: controller.running && controller.percent === 0
        }
        Label {
            text: controller.status
            color: "#9e9e9e"
            font.pixelSize: 12
        }

        Label {
            text: qsTr("Changes (%1)").arg(controller.changes.count)
            font.bold: true
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 2
            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: controller.changes
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    width: ListView.view.width
                    height: 28
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        Label {
                            text: isDelete ? "DEL" : (isNew ? "NEW" : "CHG")
                            color: isDelete ? Material.color(Material.Red)
                                            : (isNew ? Material.color(Material.Green)
                                                     : Material.color(Material.LightBlue))
                            font.family: "monospace"
                            font.pixelSize: 12
                            Layout.preferredWidth: 36
                        }
                        Label {
                            text: code
                            color: "#888888"
                            font.family: "monospace"
                            font.pixelSize: 12
                            Layout.preferredWidth: 110
                        }
                        Label {
                            text: path
                            font.family: "monospace"
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        Label {
            text: qsTr("Log")
            font.bold: true
        }
        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            padding: 2
            ScrollView {
                anchors.fill: parent
                clip: true
                TextArea {
                    readOnly: true
                    text: controller.log
                    font.family: "monospace"
                    font.pixelSize: 11
                    wrapMode: TextEdit.NoWrap
                }
            }
        }
    }
}
