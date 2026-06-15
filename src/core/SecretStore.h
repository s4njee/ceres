#pragma once

#include <QString>

/// Stores per-job rsync daemon passwords in the OS keychain instead of the
/// profile JSON, so secrets never appear in plaintext on disk.
///
/// Platform implementations:
///   macOS  — direct Security.framework Keychain API calls (not `security`
///            CLI, so the password never appears in argv / `ps` output).
///   Linux  — libsecret via `secret-tool` (a follow-up can use the C API).
///   Other  — no-op; daemon passwords are session-only for that run.
///
/// The key for each secret is the job's stable UUID, making cleanup on
/// job deletion straightforward via `remove(id)`.
/// @ingroup core
class SecretStore {
public:
    bool set(const QString &id, const QString &secret) const;
    QString get(const QString &id) const;
    bool remove(const QString &id) const;
};
