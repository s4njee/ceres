#pragma once

#include <QString>
#include <QStringList>

/// A single sync operation: the central value object of the app. It is the
/// unit the user edits in the form and ArgvBuilder translates into an rsync
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
    int bwLimitKBps = 0;            // --bwlimit=N transfer-rate cap in KB/s (0 = unlimited)
    bool ignoreExisting = false;    // --ignore-existing (skip files already on the dest)
    bool updateOnly = false;        // --update (skip files newer on the dest)

    QStringList excludes;           // order-significant -> --exclude=<pat>
    QStringList extraArgs;          // appended verbatim before SRC/DEST

    // Remote transport. Only relevant when an endpoint is remote.
    QString sshKeyPath;       // -e 'ssh -i <key>' (optional; agent/default key if empty)
    int sshPort = 0;          // -e 'ssh -p <port>' (0 = default 22)
    QString daemonPassword;   // transient: RSYNC_PASSWORD for rsync:// auth (never persisted)
    QString sshPassword;      // transient: SSH password (via SSH_ASKPASS, never in argv/JSON)
};
