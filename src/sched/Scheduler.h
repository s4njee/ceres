#pragma once

#include <QString>
#include <QStringList>

#include "core/SyncJob.h"

/// Registers a saved sync job with the OS scheduler so it runs on a timer
/// without the GUI being open.
///
/// Platform strategy:
///   macOS — writes a LaunchAgent plist to ~/Library/LaunchAgents/. launchd
///           invokes `ceres-runner --job <id>` on the configured schedule.
///   Linux — writes a systemd user timer + service to ~/.config/systemd/user/.
///           Note: user timers only fire while the user is logged in unless
///           `loginctl enable-linger` is enabled (a follow-up improvement).
///
/// A Manual schedule causes the job to be unregistered (remove its unit).
/// The `apply()` method is idempotent — calling it with an unchanged job
/// simply overwrites the existing unit file.
///
/// The plist/timer generators are pure functions, making them unit-testable
/// without actually touching the filesystem or running launchctl/systemctl.
/// @ingroup sched
class Scheduler {
public:
    bool apply(const SyncJob &job, const QString &runnerPath) const;
    bool remove(const QString &id) const;
    bool isRegistered(const QString &id) const;
    QStringList installedJobIds() const;  // ids of all registered Ceres units (for pruning)

    static QString runnerPath();                  // ceres-runner next to the app
    static QString labelFor(const QString &id);   // com.ceres.job.<id>
    static QString windowsTaskName(const QString &id);  // Ceres\<id>

    // Pure generators (unit-tested), independent of any installed state.
    static QString launchdPlist(const SyncJob &job, const QString &runnerPath, const QString &label);
    static QString systemdTimer(const SyncJob &job);
    static QString systemdService(const SyncJob &job, const QString &runnerPath);
    // Task Scheduler 2.0 XML for `schtasks /Create /XML`.
    static QString windowsTaskXml(const SyncJob &job, const QString &runnerPath);
};
