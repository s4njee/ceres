#pragma once

#include <QString>
#include <QStringList>

/// Determines how often a saved sync job runs automatically. When set to anything
/// other than Manual, the Scheduler registers an OS-level timer (launchd on macOS,
/// systemd on Linux) so the job runs even when the GUI is closed. The headless
/// `ceres-runner` binary is invoked by the OS when the timer fires.
enum class ScheduleKind { Manual, Interval, Daily, Weekly };

inline QString scheduleKindToString(ScheduleKind k)
{
    switch (k) {
    case ScheduleKind::Interval: return QStringLiteral("interval");
    case ScheduleKind::Daily:    return QStringLiteral("daily");
    case ScheduleKind::Weekly:   return QStringLiteral("weekly");
    case ScheduleKind::Manual:   break;
    }
    return QStringLiteral("manual");
}

inline ScheduleKind scheduleKindFromString(const QString &s)
{
    if (s == QLatin1String("interval")) return ScheduleKind::Interval;
    if (s == QLatin1String("daily"))    return ScheduleKind::Daily;
    if (s == QLatin1String("weekly"))   return ScheduleKind::Weekly;
    return ScheduleKind::Manual;
}

/// A single sync job / profile: the central value object of the app. It is the
/// unit the user edits in the form, ProfileStore serialises to `<id>.json`, the
/// Scheduler registers with the OS, and ArgvBuilder translates into an rsync
/// command line. Plain data with no behaviour so it stays trivially copyable and
/// testable; fields map closely to the rsync flags they drive.
/// @ingroup core
struct SyncJob {
    QString id;    // stable profile id (empty = unsaved / ad-hoc)
    QString name;  // display name in the jobs sidebar

    QString source;
    QString destination;

    // Structured flags (a small, safe subset for M1). A raw `extraArgs`
    // escape hatch keeps power users unblocked until the Advanced tier lands.
    bool archive = true;            // -a
    bool compress = false;          // -z
    bool checksum = false;          // -c
    bool deleteExtraneous = false;  // --delete
    int maxDelete = 0;              // --max-delete=N safety cap (0 = no limit)

    QStringList excludes;           // order-significant -> --exclude=<pat>
    QStringList extraArgs;          // appended verbatim before SRC/DEST

    // Remote transport. Only relevant when an endpoint is remote.
    QString sshKeyPath;       // -e 'ssh -i <key>' (optional; agent/default key if empty)
    int sshPort = 0;          // -e 'ssh -p <port>' (0 = default 22)
    QString daemonPassword;   // transient: RSYNC_PASSWORD for rsync:// auth (never persisted)

    // Scheduling (registered with the OS so it runs while the app is closed).
    ScheduleKind schedule = ScheduleKind::Manual;
    int intervalMinutes = 60;  // Interval
    int atHour = 9;            // Daily/Weekly, 0-23
    int atMinute = 0;          // Daily/Weekly, 0-59
    int weekday = 0;           // Weekly, 0=Sun .. 6=Sat (launchd convention)
};
