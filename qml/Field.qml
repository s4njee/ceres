import QtQuick
import QtQuick.Controls.Basic

TextField {
    color: Theme.textPrimary
    placeholderTextColor: Theme.textTertiary
    font.family: Theme.mono
    font.pixelSize: 13
    background: Rectangle {
        color: Theme.bgTertiary
        radius: Theme.radius
        border.width: 1
        border.color: parent.activeFocus ? Theme.accent : Theme.border
    }
}
