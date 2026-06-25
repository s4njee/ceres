import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root

    property string label
    property bool active: false
    property bool warn: false
    property bool removable: false   // show a trailing ✕ that emits removed()
    property string tooltip: ""
    signal toggled()
    signal removed()

    implicitHeight: 26
    implicitWidth: chipRow.implicitWidth + 18
    radius: Theme.radius
    color: active ? Theme.bgTertiary : "transparent"
    border.width: 1
    border.color: active ? Theme.borderStrong : Theme.border

    Row {
        id: chipRow
        anchors.centerIn: parent
        spacing: 5

        Rectangle {
            visible: root.warn
            width: 6
            height: 6
            radius: 3
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.warning
        }

        Text {
            text: root.label
            font.pixelSize: 12
            color: root.active ? Theme.textPrimary : Theme.textSecondary
        }

        Text {
            visible: root.removable
            text: "✕"
            font.pixelSize: 11
            color: removeMouse.containsMouse ? Theme.danger : Theme.textTertiary
            anchors.verticalCenter: parent.verticalCenter
            MouseArea {
                id: removeMouse
                anchors.fill: parent
                anchors.margins: -3
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.removed()
            }
        }
    }

    MouseArea {
        id: chipMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // Let the remove ✕ handle its own clicks (it sits above this via the Row).
        onClicked: root.toggled()
        z: -1
    }

    ToolTip.visible: chipMouse.containsMouse && root.tooltip.length > 0
    ToolTip.text: root.tooltip
    ToolTip.delay: 400

    Accessible.role: Accessible.Button
    Accessible.name: root.label + (root.active ? " (active)" : "")
    Accessible.description: root.tooltip
}
