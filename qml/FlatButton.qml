import QtQuick

Rectangle {
    id: root

    property string label
    property bool primary: false
    property bool danger: false
    property bool active: true
    signal clicked()

    enabled: active

    implicitHeight: 30
    implicitWidth: btnText.implicitWidth + 24
    radius: Theme.radius
    opacity: active ? 1.0 : 0.45
    color: danger ? Theme.danger : (primary ? Theme.accent : "transparent")
    border.width: (primary || danger) ? 0 : 1
    border.color: Theme.borderStrong

    scale: btnArea.pressed ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: "#ffffff"
        opacity: btnArea.pressed ? 0.15 : (btnArea.containsMouse ? 0.08 : 0.0)
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    Text {
        id: btnText
        anchors.centerIn: parent
        text: root.label
        font.pixelSize: 12
        color: (root.primary || root.danger) ? "#160a06" : Theme.textPrimary
    }

    MouseArea {
        id: btnArea
        anchors.fill: parent
        enabled: root.active
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.label
}
