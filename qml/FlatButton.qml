import QtQuick

Rectangle {
    id: root

    property string label
    property bool primary: false
    property bool danger: false
    property bool outline: false   // colored border + text, no fill (e.g. Disconnect)
    property bool active: true
    signal clicked()

    // The accent/danger colour this button is themed around.
    readonly property color tone: danger ? Theme.danger : Theme.accent

    enabled: active

    implicitHeight: 30
    implicitWidth: btnText.implicitWidth + 24
    radius: Theme.radius
    // Green is reserved for the few "primary" actions. Such a button is a solid
    // accent fill when active and a darker-green outline when disabled. An `outline`
    // button is always a coloured border + text with no fill. Every other button is a
    // neutral outline, dimmed when disabled.
    opacity: (!active && !primary && !outline) ? 0.5 : 1.0
    color: (outline || !active) ? "transparent"
                                : (danger ? Theme.danger : (primary ? Theme.accent : "transparent"))
    border.width: (active && !outline && (primary || danger)) ? 0 : 1
    border.color: outline ? root.tone
                : (!active && primary) ? Theme.accentDisabled
                : (active ? Theme.borderStrong : Theme.border)

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
        // Outline buttons use their accent/danger tone; filled buttons use light text;
        // a disabled primary uses the darker-green disabled accent, others a muted grey.
        color: root.outline ? root.tone
             : root.active ? Theme.textPrimary
             : (root.primary ? Theme.accentDisabled : Theme.textTertiary)
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
