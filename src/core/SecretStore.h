#pragma once

#include <QString>

// Stores per-job rsync daemon passwords in the OS keychain instead of the
// profile JSON. macOS: the login keychain via `security` (added with `-A` so
// both the GUI and the separate ceres-runner binary can read it on an unsigned
// build — M6 signing will scope this to a keychain access group). Linux:
// libsecret via `secret-tool`. Other platforms: no-op.
class SecretStore {
public:
    bool set(const QString &id, const QString &secret) const;
    QString get(const QString &id) const;
    bool remove(const QString &id) const;
};
