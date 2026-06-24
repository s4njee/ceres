#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "app/RemoteFs.h"
#include "core/SecretStore.h"
#include "core/SshHostStore.h"
#include "core/SyncJob.h"
#include "engine/BinaryLocator.h"
#include "models/FileListModel.h"

class TransferManager;

/// Backs the dual-pane (local ⇆ remote) browse tab.
///
/// Owns two FileListModels — a local pane read straight off the filesystem with
/// QDir, and a remote pane listed over SSH via RemoteFs. Navigation, the remote
/// file ops (mkdir/delete/rename), and download/upload all flow through here.
/// Transfers are handed to the shared TransferManager as ad-hoc SyncJobs; this
/// class never runs rsync itself.
///
/// SSH auth mirrors the rest of the app: a saved host's key/port/keychain
/// password are pulled from SshHostStore/SecretStore on connect; if key auth
/// fails, `authRequired` lets the UI prompt for a password and call
/// connectWithPassword(), optionally remembering it.
/// @ingroup app
class BrowseController : public QObject {
    Q_OBJECT
    Q_PROPERTY(FileListModel *localFiles READ localFiles CONSTANT)
    Q_PROPERTY(FileListModel *remoteFiles READ remoteFiles CONSTANT)
    Q_PROPERTY(QString localPath READ localPath NOTIFY localPathChanged)
    Q_PROPERTY(QString remotePath READ remotePath NOTIFY remotePathChanged)
    Q_PROPERTY(QString target READ target NOTIFY targetChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    BrowseController(RsyncCapabilities caps, SshHostStore hostStore, SecretStore secrets,
                     TransferManager *transfers, QObject *parent = nullptr);

    FileListModel *localFiles() { return &m_local; }
    FileListModel *remoteFiles() { return &m_remote; }
    QString localPath() const { return m_localPath; }
    QString remotePath() const { return m_remotePath; }
    QString target() const { return m_target; }
    bool connected() const { return m_connected; }
    bool busy() const { return m_busy; }

    // Connect to a saved or typed SSH target ("user@host" / "host"): resolves the
    // saved key/port/keychain password (if any), then lists the home directory.
    Q_INVOKABLE void connectHost(const QString &target);
    // Retry after key auth failed: fold `user` into the current target, authenticate
    // with `password`, and (if `remember`) persist the host + password.
    Q_INVOKABLE void connectWithPassword(const QString &user, const QString &password, bool remember);
    Q_INVOKABLE void disconnectHost();
    Q_INVOKABLE void repairKnownHostAndRetry();

    // Remote navigation / ops (no-ops when not connected).
    Q_INVOKABLE void remoteCd(const QString &name);
    Q_INVOKABLE void setRemotePath(const QString &path);
    Q_INVOKABLE void remoteUp();
    Q_INVOKABLE void remoteRefresh();
    Q_INVOKABLE void mkdirRemote(const QString &name);
    Q_INVOKABLE void deleteRemote(const QStringList &names);
    Q_INVOKABLE void renameRemote(const QString &from, const QString &to);

    // Local navigation + ops (operate on the local filesystem; no ssh).
    Q_INVOKABLE void localCd(const QString &name);
    Q_INVOKABLE void localUp();
    Q_INVOKABLE void localRefresh();
    Q_INVOKABLE void setLocalPath(const QString &path);
    Q_INVOKABLE void mkdirLocal(const QString &name);
    Q_INVOKABLE void deleteLocal(const QStringList &names);
    Q_INVOKABLE void renameLocal(const QString &from, const QString &to);

    // Transfers (each selected name becomes one queued rsync).
    Q_INVOKABLE void download(const QStringList &names);
    Q_INVOKABLE void upload(const QStringList &names);
    // Upload arbitrary local files/folders (absolute paths, e.g. from a Finder drop)
    // into the current remote directory.
    Q_INVOKABLE void uploadFiles(const QStringList &paths);

signals:
    void localPathChanged();
    void remotePathChanged();
    void targetChanged();
    void connectedChanged();
    void busyChanged();
    /// Key auth failed; the UI should prompt for a password (prefilled with `user`)
    /// and call connectWithPassword(). `host` is shown for context.
    void authRequired(const QString &host, const QString &user);
    /// A remote op or listing failed; carries a human-readable message for a toast.
    void errorOccurred(const QString &message);
    /// The remote host key changed; UI should confirm before removing known_hosts.
    void hostKeyChanged(const QString &host);
    /// A host was saved/updated (so the sidebar's SshHostListModel can reload).
    void hostsChanged();

private:
    void listRemote(const QString &dir);
    void onListed(const QString &target, const QString &path, const QList<FileEntry> &entries,
                  const QString &error);
    void setBusy(bool busy);
    void setConnected(bool connected);
    SyncJob transferJob() const;  // a SyncJob carrying the current ssh credentials

    RsyncCapabilities m_caps;
    SshHostStore m_hostStore;
    SecretStore m_secrets;
    TransferManager *m_transfers = nullptr;
    RemoteFs m_remoteFs;

    FileListModel m_local;
    FileListModel m_remote;

    QString m_target;       // "user@host" of the connected host
    QString m_sshKey;       // resolved key path (may be empty)
    int m_sshPort = 0;
    QString m_sshPassword;  // session password (empty = key/agent auth)

    QString m_localPath;
    QString m_remotePath;   // resolved absolute remote dir (trailing slash)
    QString m_pendingRemotePath;
    bool m_connected = false;
    bool m_busy = false;
};
