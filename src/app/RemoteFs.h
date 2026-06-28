#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/FileEntry.h"
#include "engine/BinaryLocator.h"

/// Remote filesystem operations over SSH for the browse tab: list a directory
/// (files and folders with size/date), mkdir, remove, rename. Each runs a short
/// POSIX `sh` script on the remote, fed to `/bin/sh` over stdin so it behaves the
/// same regardless of the remote login shell (a fish/zsh login shell would choke
/// on POSIX constructs otherwise). Mirrors the ssh plumbing PathCompleter already
/// uses (BatchMode for key auth, SSH_ASKPASS for password auth, marker-line
/// parsing of pwd/error). Lives in ceres_core: no GUI/QML dependencies.
/// @ingroup app
class RemoteFs : public QObject {
    Q_OBJECT
public:
    explicit RemoteFs(RsyncCapabilities caps, QObject *parent = nullptr);

    // All async; results arrive via signals. `target` is "user@host" (or "host").
    // `dir` is an absolute remote path or "." ; sshKey/port/password are optional
    // (empty/0 = use agent/default key). A non-empty password switches ssh to
    // password mode (BatchMode dropped) and feeds it via SSH_ASKPASS.
    void list(const QString &target, const QString &dir, const QString &sshKey, int port,
              const QString &password);
    void mkdir(const QString &target, const QString &dir, const QString &name, const QString &sshKey,
               int port, const QString &password);
    void remove(const QString &target, const QString &dir, const QStringList &names,
                const QString &sshKey, int port, const QString &password);
    void rename(const QString &target, const QString &dir, const QString &from, const QString &to,
                const QString &sshKey, int port, const QString &password);
    // Create/replace a symlink `linkName` -> `pointsTo` inside `dir` (ln -sfn). Used to
    // repoint a snapshot base's `latest` link. Reports via opFinished.
    void symlink(const QString &target, const QString &dir, const QString &linkName,
                 const QString &pointsTo, const QString &sshKey, int port, const QString &password);

    // Recursively list every non-directory entry under `dir`/`leaf` and report them
    // as paths relative to `dir` (i.e. prefixed with `leaf`), matching the form rsync
    // itemizes when copying `leaf` into a destination. Used to seed a transfer's file
    // tree up-front. `token` is echoed back on `enumerated` so the caller can route
    // the result to the right transfer. Failures (auth, unreachable) report a
    // non-empty error and an empty list — enumeration is best-effort and never
    // prompts; the transfer falls back to filling rows in as it goes.
    // Recursively size `dir`/`name` with `du -sk`; reports the total via diskUsageReady.
    void diskUsage(const QString &target, const QString &dir, const QString &name,
                   const QString &sshKey, int port, const QString &password);
    // Query free/total space on the filesystem holding `dir` with `df -Pk`; reports
    // via freeSpaceReady. Cheap enough to run alongside each directory listing.
    void freeSpace(const QString &target, const QString &dir, const QString &sshKey, int port,
                   const QString &password);

    // Pure, unit-testable: parse the body of `ls -lA` output into entries.
    static QList<FileEntry> parseLsList(const QString &lsOutput);

    // Append a short, actionable hint to a raw ssh/rsync error when the cause is
    // recognizable (auth, refused, unreachable, DNS, host key, missing path). Returns
    // the input unchanged when nothing matches. Pure and unit-testable.
    static QString friendlyError(const QString &raw);

signals:
    // Directory listing finished. `path` is the resolved absolute dir (from pwd -P,
    // trailing slash). On failure `error` is non-empty and `entries` empty.
    void listed(const QString &target, const QString &path, const QList<FileEntry> &entries,
                const QString &error);
    // mkdir/remove/rename finished. `error` empty on success.
    void opFinished(const QString &error);
    // diskUsage() finished: `bytes` is the total under `name`. Error non-empty on failure.
    void diskUsageReady(const QString &name, qint64 bytes, const QString &error);
    // freeSpace() finished: bytes available and total on the filesystem. Error on failure.
    void freeSpaceReady(qint64 availableBytes, qint64 totalBytes, const QString &error);
    // A command failed public-key auth; UI should prompt for a password and retry.
    void authRequired(const QString &target, const QString &host, const QString &user);
    // SSH found a changed known_hosts entry; UI should ask before removing it.
    void hostKeyChanged(const QString &target, const QString &host);

private:
    RsyncCapabilities m_caps;
};
