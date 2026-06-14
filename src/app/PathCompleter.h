#pragma once

#include <QObject>
#include <QString>

// Tab-completion of source/destination paths. Local paths complete synchronously
// against the filesystem; remote (user@host:path) targets complete asynchronously
// by listing the remote directory over ssh, reusing the job's key / ssh-agent.
class PathCompleter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Returns the completed path (longest common match, trailing '/' for a lone
    // directory) or the input unchanged when there's nothing to add.
    Q_INVOKABLE QString completeLocal(const QString &path) const;

    // Lists <path>* on the remote over ssh; emits remoteCompleted when it returns.
    Q_INVOKABLE void completeRemote(const QString &path, const QString &sshKey, int port);

signals:
    void remoteCompleted(const QString &input, const QString &completion);
};
