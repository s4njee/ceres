#pragma once

#include <QString>
#include <QStringList>

// A single sync job / profile. Milestone 1 carries only what local preview
// needs; SSH targets, daemon auth, ordered filter *rules*, and scheduling
// metadata get layered on in later milestones (see the plan).
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

    QStringList excludes;           // order-significant -> --exclude=<pat>
    QStringList extraArgs;          // appended verbatim before SRC/DEST

    // Remote transport. Only relevant when an endpoint is remote.
    QString sshKeyPath;       // -e 'ssh -i <key>' (optional; agent/default key if empty)
    int sshPort = 0;          // -e 'ssh -p <port>' (0 = default 22)
    QString daemonPassword;   // transient: RSYNC_PASSWORD for rsync:// auth (never persisted)
};
