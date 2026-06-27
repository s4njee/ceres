#pragma once

#include <QSettings>
#include <QString>

/// The single definition of where Ceres persists small user settings (bookmarks,
/// the external-editor command, transfer history). IniFormat/UserScope keeps one
/// portable file regardless of each platform's native default, and tests redirect
/// it via QSettings::setPath without touching real user settings.
/// @ingroup core
inline QSettings appSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Ceres"),
                     QStringLiteral("Ceres"));
}
