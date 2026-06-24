#include "app/BrowseController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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

} // namespace

BrowseController::BrowseController(RsyncCapabilities caps, SshHostStore hostStore,
                                   SecretStore secrets, TransferManager *transfers, QObject *parent)
    : QObject(parent),
      m_caps(std::move(caps)),
      m_hostStore(std::move(hostStore)),
      m_secrets(std::move(secrets)),
      m_transfers(transfers),
      m_remoteFs(m_caps)
{
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
            emit errorOccurred(error);
        remoteRefresh();  // reflect the mkdir/delete/rename either way
    });

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
        emit errorOccurred(error);
        return;
    }
    m_remote.setEntries(entries);
    m_pendingRemotePath.clear();
    if (path != m_remotePath) {
        m_remotePath = path;
        emit remotePathChanged();
    }
    setConnected(true);
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
        e.size = fi.isDir() ? 0 : fi.size();
        e.mtimeText = fi.lastModified().toString(QStringLiteral("MMM d  HH:mm"));
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

SyncJob BrowseController::transferJob() const
{
    SyncJob job;
    job.archive = true;  // -a: recursive, preserves attrs, handles files and dirs
    job.sshKeyPath = m_sshKey;
    job.sshPort = m_sshPort;
    job.sshPassword = m_sshPassword;
    return job;
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
        m_transfers->enqueue(job, QStringLiteral("down"), name);
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
        m_transfers->enqueue(job, QStringLiteral("up"), name);
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
        m_transfers->enqueue(job, QStringLiteral("up"), QFileInfo(clean).fileName());
    }
}
