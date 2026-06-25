#include "app/BrowseController.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUuid>

#include "app/TransferManager.h"
#include "core/SshKnownHosts.h"
#include "core/SyncJob.h"

namespace {

// Host portion of a "user@host" target (or the whole string if there's no user).
QString hostOf(const QString &target)
{
    const int at = target.lastIndexOf(QLatin1Char('@'));
    return at >= 0 ? target.mid(at + 1) : target;
}

QString userOf(const QString &target)
{
    const int at = target.lastIndexOf(QLatin1Char('@'));
    return at >= 0 ? target.left(at) : QString();
}

// Inject/replace the login user in a bare ssh target ("host" -> "user@host").
QString targetWithUser(const QString &target, const QString &user)
{
    if (user.isEmpty())
        return target;
    return user + QLatin1Char('@') + hostOf(target);
}

// Join a resolved remote dir (trailing slash) with a child name, no trailing slash.
QString joinRemote(const QString &dir, const QString &name)
{
    if (dir.isEmpty())
        return name;
    return dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name;
}

QString withTrailingSlash(const QString &p)
{
    return p.endsWith(QLatin1Char('/')) ? p : p + QLatin1Char('/');
}

// One settings store (favorites, editor command), independent of app-wide QCoreApplication setup.
QSettings bookmarkSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Ceres"),
                     QStringLiteral("Ceres"));
}

// Human-readable byte count (e.g. "1.4 GB"), for toast messages.
QString formatBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        ++u;
    }
    return QString::number(v, 'f', u == 0 ? 0 : 1) + QLatin1Char(' ')
            + QLatin1String(units[u]);
}

// Walk a local file or directory and return paths relative to its parent — each
// prefixed with `topName` — matching the form rsync itemizes when copying the item
// into a destination. A plain file yields just { topName }. Symlinks are skipped
// (NoSymLinks): such entries simply appear live during the transfer instead.
//
// Runs on a worker thread (see seedFromLocalWalk), so it must stay free of any
// shared mutable state — it only touches the filesystem and returns a value.
QStringList walkLocal(const QString &absPath, const QString &topName)
{
    const QFileInfo info(absPath);
    if (!info.exists())
        return {};
    if (!info.isDir())
        return {topName};

    const QString base = info.absoluteFilePath();
    const int strip = base.size() + 1;  // drop "base/" to get the path within
    QStringList rels;
    QDirIterator it(base, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString within = it.next().mid(strip);
        if (!within.isEmpty())
            rels << topName + QLatin1Char('/') + within;
    }
    return rels;
}

} // namespace

BrowseController::BrowseController(RsyncCapabilities caps, SshHostStore hostStore,
                                   SecretStore secrets, TransferManager *transfers, QObject *parent)
    : QObject(parent),
      m_caps(std::move(caps)),
      m_hostStore(std::move(hostStore)),
      m_secrets(std::move(secrets)),
      m_transfers(transfers),
      m_remoteFs(m_caps),
      m_completer(m_caps)
{
    // Re-emit remote completion results as bare remote paths (strip the "target:" prefix).
    connect(&m_completer, &PathCompleter::remoteCompleted, this,
            [this](const QString &, const QString &, const QStringList &choices) {
                const QString prefix = m_target + QLatin1Char(':');
                QStringList bare;
                bare.reserve(choices.size());
                for (const QString &c : choices)
                    bare << (c.startsWith(prefix) ? c.mid(prefix.size()) : c);
                emit remotePathCompleted(bare);
            });
    connect(&m_remoteFs, &RemoteFs::listed, this, &BrowseController::onListed);
    connect(&m_remoteFs, &RemoteFs::authRequired, this,
            [this](const QString &, const QString &host, const QString &user) {
                setBusy(false);
                emit authRequired(host, user);
            });
    connect(&m_remoteFs, &RemoteFs::hostKeyChanged, this,
            [this](const QString &, const QString &host) {
                setBusy(false);
                emit hostKeyChanged(host);
            });
    connect(&m_remoteFs, &RemoteFs::opFinished, this, [this](const QString &error) {
        if (!error.isEmpty())
            emit errorOccurred(RemoteFs::friendlyError(error));
        remoteRefresh();  // reflect the mkdir/delete/rename either way
    });
    connect(&m_remoteFs, &RemoteFs::freeSpaceReady, this,
            [this](qint64 avail, qint64 total, const QString &error) {
                // Best-effort: a df failure just leaves the indicator blank.
                const QString text = error.isEmpty() && total > 0
                        ? formatBytes(avail) + QStringLiteral(" free of ") + formatBytes(total)
                        : QString();
                if (text != m_remoteFree) {
                    m_remoteFree = text;
                    emit remoteFreeChanged();
                }
            });
    connect(&m_remoteFs, &RemoteFs::diskUsageReady, this,
            [this](const QString &name, qint64 bytes, const QString &error) {
                if (!error.isEmpty())
                    emit errorOccurred(error);
                else
                    emit infoOccurred(name + QStringLiteral(": ") + formatBytes(bytes));
            });
    // A download's remote walk finished: seed the full file list at 0%. Best-effort —
    // an enumeration error (auth/unreachable) just leaves the transfer to fill rows in
    // live, so it's swallowed here (the transfer itself surfaces real failures).
    connect(&m_remoteFs, &RemoteFs::enumerated, this,
            [this](const QString &token, const QStringList &relPaths, const QString &error) {
                if (error.isEmpty() && m_transfers && !relPaths.isEmpty())
                    m_transfers->seedFiles(token, relPaths);
            });

    // Quick-view / edit: act on the result of a fetch-to-temp transfer.
    if (m_transfers)
        connect(m_transfers, &TransferManager::transferFinished, this,
                &BrowseController::onOpenFetched);
    m_editWatcher = new QFileSystemWatcher(this);
    connect(m_editWatcher, &QFileSystemWatcher::fileChanged, this,
            &BrowseController::onEditedFileChanged);
    m_editorCommand = bookmarkSettings().value(QStringLiteral("editorCommand")).toString();

    m_localPath = QDir::homePath();
    localRefresh();
}

void BrowseController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void BrowseController::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}

void BrowseController::connectHost(const QString &target)
{
    const QString t = target.trimmed();
    if (t.isEmpty())
        return;
    m_target = t;
    emit targetChanged();

    // Resolve a saved host's key/port and remembered password, if any.
    const SshHost saved = m_hostStore.load(t);
    m_sshKey = saved.sshKeyPath;
    m_sshPort = saved.sshPort;
    m_sshPassword = saved.hasPassword ? m_secrets.get(QStringLiteral("ssh-host:") + t) : QString();

    listRemote(QStringLiteral("."));  // home; resolved path comes back via onListed
}

void BrowseController::connectWithPassword(const QString &user, const QString &password, bool remember)
{
    if (password.isEmpty() || m_target.isEmpty())
        return;

    const QString newTarget = targetWithUser(m_target, user);
    if (newTarget != m_target) {
        m_target = newTarget;
        emit targetChanged();
    }
    m_sshPassword = password;

    if (remember) {
        SshHost host;
        host.target = m_target;
        host.host = hostOf(m_target);
        host.user = userOf(m_target);
        host.label = m_target;
        host.sshKeyPath = m_sshKey;
        host.sshPort = m_sshPort;
        host.hasPassword = true;
        m_hostStore.upsert(host);
        m_secrets.set(QStringLiteral("ssh-host:") + m_target, password);
        emit hostsChanged();
    }

    listRemote(m_remotePath.isEmpty() ? QStringLiteral(".") : m_remotePath);
}

void BrowseController::disconnectHost()
{
    m_target.clear();
    m_sshKey.clear();
    m_sshPassword.clear();
    m_sshPort = 0;
    m_remotePath.clear();
    m_pendingRemotePath.clear();
    m_remote.clear();
    if (!m_remoteFree.isEmpty()) {
        m_remoteFree.clear();
        emit remoteFreeChanged();
    }
    if (!m_bookmarks.isEmpty()) {
        m_bookmarks.clear();
        emit bookmarksChanged();
    }
    setConnected(false);
    emit targetChanged();
    emit remotePathChanged();
}

void BrowseController::listRemote(const QString &dir)
{
    if (m_target.isEmpty())
        return;
    m_pendingRemotePath = dir;
    setBusy(true);
    m_remoteFs.list(m_target, dir, m_sshKey, m_sshPort, m_sshPassword);
}

void BrowseController::onListed(const QString & /*target*/, const QString &path,
                                const QList<FileEntry> &entries, const QString &error)
{
    setBusy(false);
    if (!error.isEmpty()) {
        // If an established session's listing fails because the connection dropped
        // (idle NAT, sleep, flaky link), transparently retry the same listing once —
        // ssh will re-establish a fresh connection.
        static const char *kDropMarkers[] = {"Connection closed", "Connection reset",
                                             "Broken pipe", "Connection timed out",
                                             "closed by remote host", "Connection refused"};
        bool dropped = false;
        for (const char *m : kDropMarkers)
            dropped = dropped || error.contains(QLatin1String(m), Qt::CaseInsensitive);
        if (m_connected && dropped && !m_reconnecting) {
            m_reconnecting = true;
            listRemote(m_pendingRemotePath.isEmpty()
                           ? (m_remotePath.isEmpty() ? QStringLiteral(".") : m_remotePath)
                           : m_pendingRemotePath);
            return;
        }
        m_reconnecting = false;
        emit errorOccurred(RemoteFs::friendlyError(error));
        return;
    }
    m_reconnecting = false;  // a clean listing means the (re)connection is healthy
    m_remote.setEntries(entries);
    m_pendingRemotePath.clear();
    if (path != m_remotePath) {
        m_remotePath = path;
        emit remotePathChanged();
    }
    setConnected(true);
    reloadBookmarks();  // favorites are per-target; load them once connected
    // Refresh the free-space indicator for the now-current directory (cheap df pass).
    m_remoteFs.freeSpace(m_target, m_remotePath, m_sshKey, m_sshPort, m_sshPassword);
}

void BrowseController::remoteCd(const QString &name)
{
    if (m_connected)
        listRemote(joinRemote(m_remotePath, name));
}

void BrowseController::repairKnownHostAndRetry()
{
    if (m_target.isEmpty())
        return;

    const KnownHostRepairResult result = SshKnownHosts::removeHost(m_caps, m_target, m_sshPort);
    if (!result.ok) {
        emit errorOccurred(result.message);
        return;
    }
    emit errorOccurred(result.message);
    listRemote(m_pendingRemotePath.isEmpty()
                   ? (m_remotePath.isEmpty() ? QStringLiteral(".") : m_remotePath)
                   : m_pendingRemotePath);
}

void BrowseController::setRemotePath(const QString &path)
{
    if (!m_connected)
        return;
    const QString clean = path.trimmed();
    if (clean.isEmpty())
        return;
    if (clean.startsWith(QLatin1Char('/')) || clean.startsWith(QLatin1Char('~')))
        listRemote(clean);
    else
        listRemote(joinRemote(m_remotePath, clean));
}

void BrowseController::remoteUp()
{
    if (m_connected)
        listRemote(joinRemote(m_remotePath, QStringLiteral("..")));  // pwd -P resolves it
}

void BrowseController::completeRemotePath(const QString &partial)
{
    if (!m_connected)
        return;
    // Resolve a relative fragment against the current directory; absolute/~ pass through.
    const QString full = (partial.startsWith(QLatin1Char('/')) || partial.startsWith(QLatin1Char('~')))
                             ? partial
                             : joinRemote(m_remotePath, partial);
    m_completer.completeRemote(m_target + QLatin1Char(':') + full, m_sshKey, m_sshPort,
                               /*maxChoices=*/30, m_sshPassword);
}

void BrowseController::remoteRefresh()
{
    if (!m_target.isEmpty())
        listRemote(m_remotePath.isEmpty() ? QStringLiteral(".") : m_remotePath);
}

void BrowseController::mkdirRemote(const QString &name)
{
    if (m_connected && !name.trimmed().isEmpty())
        m_remoteFs.mkdir(m_target, m_remotePath, name, m_sshKey, m_sshPort, m_sshPassword);
}

void BrowseController::deleteRemote(const QStringList &names)
{
    if (m_connected && !names.isEmpty())
        m_remoteFs.remove(m_target, m_remotePath, names, m_sshKey, m_sshPort, m_sshPassword);
}

void BrowseController::renameRemote(const QString &from, const QString &to)
{
    if (m_connected && !from.isEmpty() && !to.trimmed().isEmpty())
        m_remoteFs.rename(m_target, m_remotePath, from, to, m_sshKey, m_sshPort, m_sshPassword);
}

void BrowseController::localCd(const QString &name)
{
    setLocalPath(QDir(m_localPath).filePath(name));
}

void BrowseController::localUp()
{
    QDir dir(m_localPath);
    if (dir.cdUp())
        setLocalPath(dir.absolutePath());
}

void BrowseController::setLocalPath(const QString &path)
{
    const QString input = path.trimmed();
    if (input.isEmpty())
        return;
    const QString candidate = QDir::isAbsolutePath(input) ? input : QDir(m_localPath).filePath(input);
    const QString clean = QDir(candidate).absolutePath();
    if (!QFileInfo(clean).isDir()) {
        emit errorOccurred(QStringLiteral("No such folder: %1").arg(input));
        return;
    }
    if (clean != m_localPath) {
        m_localPath = clean;
        emit localPathChanged();
    }
    localRefresh();
}

void BrowseController::localRefresh()
{
    QList<FileEntry> entries;
    const QFileInfoList infos =
        QDir(m_localPath)
            .entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QFileInfo &fi : infos) {
        FileEntry e;
        e.name = fi.fileName();
        e.isDir = fi.isDir();
        e.isSymlink = fi.isSymLink();
        if (e.isSymlink)
            e.linkTarget = fi.symLinkTarget();
        e.size = fi.isDir() ? 0 : fi.size();
        e.mtimeText = fi.lastModified().toString(QStringLiteral("MMM d  HH:mm"));
        e.mtime = fi.lastModified().toSecsSinceEpoch();
        entries.append(e);
    }
    m_local.setEntries(entries);
}

void BrowseController::mkdirLocal(const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    if (!QDir(m_localPath).mkdir(name.trimmed()))
        emit errorOccurred(QStringLiteral("Could not create folder"));
    localRefresh();
}

void BrowseController::deleteLocal(const QStringList &names)
{
    QDir dir(m_localPath);
    for (const QString &name : names) {
        if (name.isEmpty())
            continue;
        const QString path = dir.filePath(name);
        QFileInfo fi(path);
        const bool ok = (fi.isDir() && !fi.isSymLink()) ? QDir(path).removeRecursively()
                                                         : QFile::remove(path);
        if (!ok)
            emit errorOccurred(QStringLiteral("Could not delete %1").arg(name));
    }
    localRefresh();
}

void BrowseController::renameLocal(const QString &from, const QString &to)
{
    if (from.isEmpty() || to.trimmed().isEmpty())
        return;
    QDir dir(m_localPath);
    if (!dir.rename(from, to.trimmed()))
        emit errorOccurred(QStringLiteral("Could not rename %1").arg(from));
    localRefresh();
}

void BrowseController::reloadBookmarks()
{
    QStringList next;
    if (!m_target.isEmpty()) {
        QSettings s = bookmarkSettings();
        s.beginGroup(QStringLiteral("bookmarks"));
        next = s.value(m_target).toStringList();
        s.endGroup();
    }
    if (next != m_bookmarks) {
        m_bookmarks = next;
        emit bookmarksChanged();
    }
}

void BrowseController::addBookmark()
{
    if (!m_connected || m_target.isEmpty() || m_remotePath.isEmpty())
        return;
    QSettings s = bookmarkSettings();
    s.beginGroup(QStringLiteral("bookmarks"));
    QStringList list = s.value(m_target).toStringList();
    if (!list.contains(m_remotePath)) {
        list << m_remotePath;
        list.sort();
        s.setValue(m_target, list);
    }
    s.endGroup();
    reloadBookmarks();
}

void BrowseController::removeBookmark(const QString &path)
{
    if (m_target.isEmpty())
        return;
    QSettings s = bookmarkSettings();
    s.beginGroup(QStringLiteral("bookmarks"));
    QStringList list = s.value(m_target).toStringList();
    if (list.removeAll(path) > 0)
        s.setValue(m_target, list);
    s.endGroup();
    reloadBookmarks();
}

void BrowseController::gotoBookmark(const QString &path)
{
    if (m_connected && !path.isEmpty())
        listRemote(path);
}

void BrowseController::remoteFolderSize(const QString &name)
{
    if (!m_connected || name.isEmpty())
        return;
    m_remoteFs.diskUsage(m_target, m_remotePath, name, m_sshKey, m_sshPort, m_sshPassword);
}

namespace {
// Open `path` with the configured editor command (split into program + args), or the
// OS default opener when no editor is set.
void openPath(const QString &editorCommand, const QString &path)
{
    if (!editorCommand.trimmed().isEmpty()) {
        QStringList parts = QProcess::splitCommand(editorCommand);
        if (!parts.isEmpty()) {
            const QString prog = parts.takeFirst();
            parts << path;
            QProcess::startDetached(prog, parts);
            return;
        }
    }
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {path});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("cmd"),
                            {QStringLiteral("/c"), QStringLiteral("start"), QString(),
                             QDir::toNativeSeparators(path)});
#else
    QProcess::startDetached(QStringLiteral("xdg-open"), {path});
#endif
}
}  // namespace

void BrowseController::setEditorCommand(const QString &cmd)
{
    if (cmd == m_editorCommand)
        return;
    m_editorCommand = cmd;
    bookmarkSettings().setValue(QStringLiteral("editorCommand"), cmd);
    emit editorCommandChanged();
}

void BrowseController::quickViewRemote(const QString &name)
{
    fetchForOpen(name, /*edit=*/false);
}

void BrowseController::editRemote(const QString &name)
{
    fetchForOpen(name, /*edit=*/true);
}

void BrowseController::fetchForOpen(const QString &name, bool edit)
{
    if (!m_connected || !m_transfers || name.isEmpty())
        return;
    // Unique temp dir per open so concurrent views of same-named files don't collide.
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("ceres-open-")
                                      + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(dir);

    SyncJob job = transferJob();
    job.source = m_target + QLatin1Char(':') + joinRemote(m_remotePath, name);
    job.destination = withTrailingSlash(dir);
    const QString id = m_transfers->enqueue(job, QStringLiteral("down"), name);
    m_pendingOpens.insert(id, PendingOpen{QDir(dir).filePath(name), edit,
                                          m_target + QLatin1Char(':')
                                              + withTrailingSlash(m_remotePath)});
}

void BrowseController::onOpenFetched(const QString &id, bool success)
{
    const auto it = m_pendingOpens.constFind(id);
    if (it == m_pendingOpens.cend())
        return;  // not one of ours
    const PendingOpen pending = it.value();
    m_pendingOpens.erase(it);

    if (!success || !QFileInfo::exists(pending.localPath)) {
        if (success)  // transfer "succeeded" but the file isn't there (e.g. a dir)
            emit errorOccurred(QStringLiteral("Could not open the downloaded file"));
        return;
    }

    if (pending.edit) {
        // Watch the temp copy; a save re-uploads it to the original remote directory.
        m_editTargets.insert(pending.localPath, pending.remoteDir);
        m_editWatcher->addPath(pending.localPath);
    }
    openPath(pending.edit ? m_editorCommand : QString(), pending.localPath);
}

void BrowseController::onEditedFileChanged(const QString &localPath)
{
    const auto it = m_editTargets.constFind(localPath);
    if (it == m_editTargets.cend() || !m_transfers)
        return;
    if (!QFileInfo::exists(localPath))
        return;  // a transient delete during the editor's save-replace; wait for re-add

    // Many editors save by replacing the file, which drops the watch — re-arm it.
    if (!m_editWatcher->files().contains(localPath))
        m_editWatcher->addPath(localPath);

    SyncJob job = transferJob();
    job.source = localPath;
    job.destination = it.value();  // "target:/remote/dir/"
    m_transfers->enqueue(job, QStringLiteral("up"), QFileInfo(localPath).fileName());
    emit infoOccurred(QFileInfo(localPath).fileName() + QStringLiteral(" — uploading changes"));
}

void BrowseController::revealLocal(const QString &name)
{
    if (name.isEmpty())
        return;
    const QString path = QDir(m_localPath).filePath(name);
    if (!QFileInfo::exists(path)) {
        emit errorOccurred(QStringLiteral("No such item: %1").arg(name));
        return;
    }
#if defined(Q_OS_MACOS)
    // -R reveals (selects) the item in its containing Finder window.
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#elif defined(Q_OS_WIN)
    // explorer wants a single "/select,<native-path>" argument.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
#else
    // No portable "reveal and select" on Linux; open the containing folder with the
    // freedesktop opener (keeps ceres_core free of any Qt GUI dependency).
    QProcess::startDetached(QStringLiteral("xdg-open"), {QFileInfo(path).absolutePath()});
#endif
}

SyncJob BrowseController::transferJob() const
{
    SyncJob job;
    job.archive = true;  // -a: recursive, preserves attrs, handles files and dirs
    job.sshKeyPath = m_sshKey;
    job.sshPort = m_sshPort;
    job.sshPassword = m_sshPassword;
    return job;
}

void BrowseController::seedFromLocalWalk(const QString &id, const QString &absPath,
                                         const QString &topName)
{
    // Walk on a pool thread so a large drop never stutters the UI, then hop back to
    // the GUI thread to mutate the model. invokeMethod with `this` as context drops
    // the call safely if the controller is gone by the time the walk finishes.
    QThreadPool::globalInstance()->start([this, id, absPath, topName] {
        const QStringList rels = walkLocal(absPath, topName);
        QMetaObject::invokeMethod(this, [this, id, rels] {
            if (m_transfers && !rels.isEmpty())
                m_transfers->seedFiles(id, rels);
        }, Qt::QueuedConnection);
    });
}

void BrowseController::download(const QStringList &names)
{
    if (!m_connected || !m_transfers)
        return;
    const QString dest = withTrailingSlash(m_localPath);
    for (const QString &name : names) {
        if (name.isEmpty())
            continue;
        SyncJob job = transferJob();
        job.source = m_target + QLatin1Char(':') + joinRemote(m_remotePath, name);
        job.destination = dest;
        const QString id = m_transfers->enqueue(job, QStringLiteral("down"), name);
        // Walk the remote tree so the whole file list shows at 0% immediately; the
        // result arrives async (RemoteFs::enumerated) and never blocks the transfer.
        m_remoteFs.enumerate(id, m_target, m_remotePath, name, m_sshKey, m_sshPort, m_sshPassword);
    }
}

void BrowseController::upload(const QStringList &names)
{
    if (!m_connected || !m_transfers)
        return;
    const QString dest = m_target + QLatin1Char(':') + withTrailingSlash(m_remotePath);
    for (const QString &name : names) {
        if (name.isEmpty())
            continue;
        SyncJob job = transferJob();
        job.source = QDir(m_localPath).filePath(name);
        job.destination = dest;
        const QString id = m_transfers->enqueue(job, QStringLiteral("up"), name);
        // Local source: walk off-thread and seed the file list when it returns.
        seedFromLocalWalk(id, job.source, name);
    }
}

void BrowseController::uploadFiles(const QStringList &paths)
{
    if (!m_connected || !m_transfers)
        return;
    const QString dest = m_target + QLatin1Char(':') + withTrailingSlash(m_remotePath);
    for (const QString &path : paths) {
        const QString clean = path.trimmed();
        if (clean.isEmpty())
            continue;
        SyncJob job = transferJob();
        job.source = clean;  // absolute local path from the drop
        job.destination = dest;
        const QString display = QFileInfo(clean).fileName();
        const QString id = m_transfers->enqueue(job, QStringLiteral("up"), display);
        seedFromLocalWalk(id, clean, display);
    }
}
