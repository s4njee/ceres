#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include "app/PathCompleter.h"
#include "app/RemoteFs.h"
#include "core/SecretStore.h"
#include "core/SshHostStore.h"
#include "core/SyncJob.h"
#include "engine/BinaryLocator.h"
#include "models/FileListModel.h"

class TransferManager;
class QFileSystemWatcher;

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
    Q_PROPERTY(QString remoteFree READ remoteFree NOTIFY remoteFreeChanged)
    Q_PROPERTY(QString localFree READ localFree NOTIFY localFreeChanged)
    Q_PROPERTY(QStringList bookmarks READ bookmarks NOTIFY bookmarksChanged)
    // Snapshot timeline for the current location: the snapshot names in the base (newest
    // first), and which one we're currently inside (empty when at the base itself).
    Q_PROPERTY(QStringList remoteSnapshots READ remoteSnapshots NOTIFY snapshotsChanged)
    Q_PROPERTY(QString activeSnapshot READ activeSnapshot NOTIFY snapshotsChanged)
    Q_PROPERTY(QString editorCommand READ editorCommand WRITE setEditorCommand NOTIFY editorCommandChanged)
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
    QString remoteFree() const { return m_remoteFree; }  // "X free of Y" for the remote header
    QString localFree() const { return m_localFree; }    // same, for the local filesystem
    QStringList bookmarks() const { return m_bookmarks; }  // saved paths for the connected host

    // Favorite remote paths, persisted per host (QSettings). Add/remove the current
    // directory; jump to a saved one.
    Q_INVOKABLE void addBookmark();
    Q_INVOKABLE void removeBookmark(const QString &path);
    Q_INVOKABLE void gotoBookmark(const QString &path);

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
    // Async path completion for the remote pane: lists "<partial>*" on the host and
    // reports matching paths via remotePathCompleted (bare remote paths, no target:).
    Q_INVOKABLE void completeRemotePath(const QString &partial);
    Q_INVOKABLE void mkdirRemote(const QString &name);
    Q_INVOKABLE void deleteRemote(const QStringList &names);
    Q_INVOKABLE void renameRemote(const QString &from, const QString &to);
    // Recursively compute the size of a remote folder/file; result arrives via infoOccurred.
    Q_INVOKABLE void remoteFolderSize(const QString &name);
    // Snapshot a local folder into the current remote directory (the snapshot base):
    // rsync into a new timestamped subdir, hardlinking unchanged files from the latest
    // prior snapshot, then repoint the base's `latest` symlink. See core/Snapshot.
    Q_INVOKABLE void snapshotToRemote(const QString &localName);
    // How many snapshot subdirectories the current remote directory already holds (so
    // the UI can label the action / show the count).
    Q_INVOKABLE int snapshotCount() const;
    QStringList remoteSnapshots() const { return m_remoteSnapshots; }
    QString activeSnapshot() const { return m_activeSnapshot; }
    // Navigate to a snapshot (by name) within the current snapshot base.
    Q_INVOKABLE void openSnapshot(const QString &name);
    // Navigate to the snapshot base itself (leave a snapshot, back to the timeline root).
    Q_INVOKABLE void openSnapshotBase();
    // Download a remote file to a temp dir and open it: quickViewRemote with the OS
    // default app, editRemote with the configured editor (or default) plus a watch that
    // re-uploads on save.
    Q_INVOKABLE void quickViewRemote(const QString &name);
    Q_INVOKABLE void editRemote(const QString &name);

    QString editorCommand() const { return m_editorCommand; }  // empty = OS default opener
    void setEditorCommand(const QString &cmd);

    // Local navigation + ops (operate on the local filesystem; no ssh).
    Q_INVOKABLE void localCd(const QString &name);
    Q_INVOKABLE void localUp();
    Q_INVOKABLE void localRefresh();
    Q_INVOKABLE void setLocalPath(const QString &path);
    Q_INVOKABLE void mkdirLocal(const QString &name);
    Q_INVOKABLE void deleteLocal(const QStringList &names);
    Q_INVOKABLE void renameLocal(const QString &from, const QString &to);
    // Reveal a local item in the OS file manager (Finder / Explorer / default).
    Q_INVOKABLE void revealLocal(const QString &name);

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
    void remoteFreeChanged();
    void localFreeChanged();
    void bookmarksChanged();
    void snapshotsChanged();
    /// Key auth failed; the UI should prompt for a password (prefilled with `user`)
    /// and call connectWithPassword(). `host` is shown for context.
    void authRequired(const QString &host, const QString &user);
    /// A remote op or listing failed; carries a human-readable message for a toast.
    void errorOccurred(const QString &message);
    /// A non-error status result (e.g. a computed folder size) for the transient toast.
    void infoOccurred(const QString &message);
    void editorCommandChanged();
    /// Remote path-completion results (bare remote paths) for the path field.
    void remotePathCompleted(const QStringList &choices);
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
    void reloadBookmarks();  // refresh m_bookmarks for the current target from QSettings
    // Download `name` into a temp dir via the transfer queue; on success the pending
    // entry is opened (and, for edits, watched for re-upload).
    void fetchForOpen(const QString &name, bool edit);
    void onOpenFetched(const QString &id, bool success);
    void onEditedFileChanged(const QString &localPath);
    SyncJob transferJob() const;  // a SyncJob carrying the current ssh credentials
    // Walk a local source off the UI thread and seed transfer `id`'s file tree with
    // the result (delivered back on the GUI thread). `topName` is the rsync-relative
    // prefix (the item's own name). For remote sources the walk is RemoteFs::enumerate.
    void seedFromLocalWalk(const QString &id, const QString &absPath, const QString &topName);

    RsyncCapabilities m_caps;
    SshHostStore m_hostStore;
    SecretStore m_secrets;
    TransferManager *m_transfers = nullptr;
    RemoteFs m_remoteFs;
    PathCompleter m_completer;  // remote path completion (declared after m_caps)

    FileListModel m_local;
    FileListModel m_remote;

    QString m_target;       // "user@host" of the connected host
    QString m_sshKey;       // resolved key path (may be empty)
    int m_sshPort = 0;
    QString m_sshPassword;  // session password (empty = key/agent auth)

    QString m_remoteFree;   // free/total summary for the connected remote filesystem
    QString m_localFree;    // free/total summary for the local filesystem
    QStringList m_bookmarks;  // favorite paths for the current target (from QSettings)

    QString m_editorCommand;  // external editor command (empty = OS default opener)
    struct PendingOpen { QString localPath; bool edit = false; QString remoteDir; };
    QHash<QString, PendingOpen> m_pendingOpens;  // transfer id -> file to open when done
    QHash<QString, QString> m_editTargets;       // watched localPath -> remote upload dir
    QFileSystemWatcher *m_editWatcher = nullptr;

    // Snapshots awaiting completion: transfer id -> (base dir, new snapshot name), so the
    // base's `latest` symlink can be repointed once the rsync finishes.
    struct PendingSnapshot { QString base; QString name; };
    QHash<QString, PendingSnapshot> m_pendingSnapshots;

    // Snapshot timeline context, recomputed on each remote listing. The base "sticks"
    // so the timeline stays visible (with the active one highlighted) while browsing
    // inside a snapshot.
    void updateSnapshotContext();
    QString m_snapshotBase;        // base dir (trailing slash) holding the snapshots
    QStringList m_remoteSnapshots; // snapshot names in the base, newest first
    QString m_activeSnapshot;      // the snapshot we're currently inside, or empty

    QString m_localPath;
    QString m_remotePath;   // resolved absolute remote dir (trailing slash)
    QString m_pendingRemotePath;
    bool m_connected = false;
    bool m_busy = false;
    bool m_reconnecting = false;  // a dropped-connection auto-retry is in flight (one shot)
};
