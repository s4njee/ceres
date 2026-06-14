import QtQuick

Rectangle {
    id: root

    property string label
    property bool primary: false
    property bool danger: false
    property bool active: true
    signal clicked()

    implicitHeight: 30
    implicitWidth: btnText.implicitWidth + 24
    radius: Theme.radius
    opacity: active ? 1.0 : 0.45
    color: danger ? Theme.danger : (primary ? Theme.accent : "transparent")
    border.width: (primary || danger) ? 0 : 1
    border.color: Theme.borderStrong

    Text {
        id: btnText
        anchors.centerIn: parent
        text: root.label
        font.pixelSize: 12
        color: (root.primary || root.danger) ? "#160a06" : Theme.textPrimary
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.active
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
