#pragma once

#include <QObject>
#include <QString>

/// Posts native desktop notifications via the platform's CLI tool, shelled out with
/// QProcess so ceres_core keeps its Core/Network-only footprint (no Qt GUI/Widgets).
/// macOS uses osascript, Linux uses notify-send; other platforms are a no-op.
/// @ingroup app
class Notifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Show a transient system notification. Best-effort: silently does nothing if the
    // platform tool is missing.
    Q_INVOKABLE void notify(const QString &title, const QString &body);
};
