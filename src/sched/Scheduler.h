#pragma once

#include <QString>
#include <QStringList>

#include "core/SyncJob.h"

// Registers a saved job with the OS scheduler so `ceres-runner --job <id>` runs
// on schedule without the app open: a launchd LaunchAgent on macOS, a systemd
// user timer on Linux. (Linux user timers only fire while the user is logged in
// unless `loginctl enable-linger` is enabled — a follow-up.) A Manual schedule
// unregisters the job.
class Scheduler {
public:
    bool apply(const SyncJob &job, const QString &runnerPath) const;
    bool remove(const QString &id) const;
    bool isRegistered(const QString &id) const;
    QStringList installedJobIds() const;  // ids of all registered Ceres units (for pruning)

    static QString runnerPath();                  // ceres-runner next to the app
    static QString labelFor(const QString &id);   // com.ceres.job.<id>

    // Pure generators (unit-tested), independent of any installed state.
    static QString launchdPlist(const SyncJob &job, const QString &runnerPath, const QString &label);
    static QString systemdTimer(const SyncJob &job);
    static QString systemdService(const SyncJob &job, const QString &runnerPath);
};
