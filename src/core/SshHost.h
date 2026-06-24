#pragma once

#include <QString>

/// A saved SSH target independent of any single sync profile.
///
/// `target` is the rsync/ssh endpoint prefix before the colon, e.g. `host` or
/// `user@host`. Passwords are never stored here; `hasPassword` only records
/// whether a matching keychain entry was intentionally remembered.
/// @ingroup core
struct SshHost {
    QString target;
    QString host;
    QString user;
    QString label;
    QString sshKeyPath;
    int sshPort = 0;
    bool hasPassword = false;
};
