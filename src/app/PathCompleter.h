#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "engine/BinaryLocator.h"

/// Tab-completion of source/destination paths. Local paths complete synchronously
/// against the filesystem; remote (`user@host:path`) targets complete asynchronously
/// by listing the remote directory over ssh, reusing the job's key / ssh-agent.
/// @ingroup app
class PathCompleter : public QObject {
    Q_OBJECT
public:
    explicit PathCompleter(QObject *parent = nullptr);
    explicit PathCompleter(RsyncCapabilities caps, QObject *parent = nullptr);

    /// Returns the completed path (longest common match, trailing '/' for a lone
    /// directory) or the input unchanged when there's nothing to add.
    Q_INVOKABLE QString completeLocal(const QString &path) const;

    /// Returns full local completion choices, or an empty list if there are none
    /// or more than maxChoices. Directories include a trailing slash.
    Q_INVOKABLE QStringList localChoices(const QString &path, int maxChoices) const;

    /// Lists `<path>*` on the remote over ssh; emits remoteCompleted when it returns.
    /// A non-empty `sshPassword` authenticates with a password (via SSH_ASKPASS).
    Q_INVOKABLE void completeRemote(const QString &path, const QString &sshKey, int port,
                                    int maxChoices, const QString &sshPassword = QString());

    /// Lists child directories for an SSH endpoint; emits remoteBrowseCompleted. When
    /// the connection fails public-key auth and no `sshPassword` was supplied, emits
    /// remoteAuthRequired instead so the UI can prompt for a password and retry.
    Q_INVOKABLE void browseRemote(const QString &path, const QString &sshKey, int port,
                                  int maxEntries, const QString &sshPassword = QString());

signals:
    void remoteCompleted(const QString &input, const QString &completion, const QStringList &choices);
    void remoteBrowseCompleted(const QString &input, const QString &current,
                               const QStringList &directories, const QString &error);
    /// A browse attempt failed key authentication; the UI should prompt for a
    /// password (prefilled with `user`) and call browseRemote again with it. `host`
    /// is shown for context. Mirrors JobController::sshAuthRequired.
    void remoteAuthRequired(const QString &input, const QString &host, const QString &user);

private:
    RsyncCapabilities m_caps;
};
