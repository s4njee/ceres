#include "app/JobController.h"

#include <QCryptographicHash>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <utility>

#include "core/Endpoint.h"
#include "core/Peer.h"
#include "core/SshHost.h"
#include "core/SyncJob.h"
#include "engine/RsyncProcessEngine.h"
#include "net/DiscoveryService.h"
#include "net/NetworkUtils.h"

namespace {

QStringList stringListFromVariant(const QVariant &value)
{
    if (value.canConvert<QStringList>())
        return value.toStringList();

    QStringList result;
    const QVariantList list = value.toList();
    for (const QVariant &item : list)
        result << item.toString();
    return result;
}

} // namespace

JobController::JobController(QObject *parent)
    : JobController(BinaryLocator::locateRsync(), nullptr, ProfileStore{}, SecretStore{},
                    Scheduler{}, SshHostStore{}, true, parent)
{
}

JobController::JobController(RsyncCapabilities caps, SyncEngine *engine, ProfileStore store,
                             SecretStore secrets, Scheduler scheduler, SshHostStore sshHostStore,
                             bool startNetworkServices,
                             QObject *parent)
    : QObject(parent),
      m_caps(std::move(caps)),
      m_store(std::move(store)),
      m_secrets(std::move(secrets)),
      m_sshHostStore(std::move(sshHostStore)),
      m_scheduler(std::move(scheduler))
{
    m_hostName = QHostInfo::localHostName();
    m_hostAddress = NetworkUtils::primaryAddress();

    if (startNetworkServices) {
        // The primary IP can change (Wi-Fi switch, VPN up/down), so re-check it and
        // update the corner pill live instead of freezing the launch-time value.
        m_addressTimer.setInterval(10000);
        connect(&m_addressTimer, &QTimer::timeout, this, [this] {
            const QString addr = NetworkUtils::primaryAddress();
            if (addr != m_hostAddress) {
                m_hostAddress = addr;
                emit hostAddressChanged();
            }
        });
        m_addressTimer.start();
    }

    const QList<SyncJob> loaded = m_store.loadAll();
    m_jobs.setJobs(loaded);
    m_sshHosts.setHosts(m_sshHostStore.loadAll(), loaded);
    // Re-register only schedules whose OS unit went missing (e.g. deleted out of
    // band). Re-applying an already-registered job would reload its launchd agent
    // and reset a StartInterval countdown on every launch; saveJob() already
    // re-applies when a schedule is edited.
    QStringList loadedIds;
    QStringList liveScheduled;
    for (const SyncJob &j : loaded) {
        loadedIds << j.id;
        if (j.schedule != ScheduleKind::Manual) {
            liveScheduled << j.id;
            if (!m_scheduler.isRegistered(j.id))
                m_scheduler.apply(j, Scheduler::runnerPath());
        }
    }
    // Prune an installed unit only when its job is genuinely gone (no profile file
    // at all) or loaded as Manual. A profile that is present but failed to load
    // (locked / corrupt this run) is left alone, so a transient read error can't
    // silently destroy a live schedule.
    const QStringList onDisk = m_store.presentJobIds();
    for (const QString &id : m_scheduler.installedJobIds()) {
        if (liveScheduled.contains(id))
            continue;
        if (loadedIds.contains(id) || !onDisk.contains(id))
            m_scheduler.remove(id);
    }

    if (startNetworkServices) {
        Peer self;
        self.id = qEnvironmentVariable("CERES_NODE_ID");  // override lets two dev instances coexist
        if (self.id.isEmpty())
            self.id = QString::fromLatin1(QSysInfo::machineUniqueId());
        if (self.id.isEmpty())
            self.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        self.name = m_hostName;
        self.addresses = NetworkUtils::gatherAddresses();
        self.os = NetworkUtils::osName();
        self.version = QStringLiteral("0.1");
        self.accepts = {QStringLiteral("ssh")};
        m_discovery = new DiscoveryService(&m_peers, self, this);
        m_discovery->start();
    }

    m_engine = engine ? engine : new RsyncProcessEngine(m_caps, this);

    connect(m_engine, &SyncEngine::change, &m_changes, &ChangeListModel::append);

    connect(m_engine, &SyncEngine::progress, this, [this](const ProgressInfo &p) {
        if (!m_activeDryRun)
            setStatus(QStringLiteral("Transferring…"));
        const bool changed = m_percent != p.percent || m_speed != p.rate;
        if (changed) {
            m_percent = p.percent;
            m_speed = p.rate;
            emit progressChanged();
        }
    });

    connect(m_engine, &SyncEngine::log, this, [this](const QString &l) { appendLog(l); });
    connect(m_engine, &SyncEngine::stats, this, [this](const QString &l) { appendLog(l); });
    connect(m_engine, &SyncEngine::errorOutput, this, [this](const QString &l) {
        m_runStderr += l;
        m_runStderr += QLatin1Char('\n');
        appendLog(QStringLiteral("[stderr] ") + l);
    });

    connect(m_engine, &SyncEngine::started, this, [this] {
        m_changes.clear();
        m_logLines.clear();
        m_runStderr.clear();
        emit logChanged();
        m_percent = 0;
        m_speed.clear();
        emit progressChanged();
        setRunning(true);
        setStatus(QStringLiteral("Scanning…"));
    });

    connect(m_engine, &SyncEngine::finished, this, [this](int code, bool crashed) {
        setRunning(false);
        if (crashed) {
            if (!m_activeDryRun)
                m_lastPreviewFingerprint.clear();
            setStatus(QStringLiteral("Cancelled / interrupted"));
        } else if (code == 0) {
            if (m_activeDryRun) {
                m_lastPreviewFingerprint = m_activeFingerprint;
                setStatus(QStringLiteral("Preview complete — %1 change(s)").arg(m_changes.count()));
            } else {
                m_lastPreviewFingerprint.clear();
                m_percent = 100;
                emit progressChanged();
                setStatus(QStringLiteral("Sync complete — %1 item(s)").arg(m_changes.count()));
            }
        } else {
            if (!m_activeDryRun)
                m_lastPreviewFingerprint.clear();

            // SSH public-key auth failed and we haven't tried a password yet — prompt
            // for credentials and retry, instead of just reporting a connection error.
            const bool authFailed = m_runStderr.contains(QStringLiteral("Permission denied"))
                || m_runStderr.contains(QStringLiteral("Authentication failed"));
            if (authFailed && !m_activeUsedPassword && !m_activeRemote.isEmpty()) {
                const Endpoint e = EndpointParser::parse(m_activeRemote);
                const int at = e.sshTarget.lastIndexOf(QLatin1Char('@'));
                const QString host = at >= 0 ? e.sshTarget.mid(at + 1) : e.sshTarget;
                const QString user = at >= 0 ? e.sshTarget.left(at) : QString();
                setStatus(QStringLiteral("Key authentication failed — enter a password for %1")
                              .arg(host));
                m_activeFingerprint.clear();
                emit sshAuthRequired(host, user);
                return;
            }

            switch (code) {
            case 5:
                setStatus(QStringLiteral("Authentication failed (rsync daemon)"));
                break;
            case 10:
            case 11:
            case 12:
                setStatus(QStringLiteral("Connection/protocol error (code %1) — check the host, "
                                         "SSH key, or that the host key is trusted").arg(code));
                break;
            case 23:
            case 24:
                setStatus(QStringLiteral("Finished with some files skipped (code %1)").arg(code));
                break;
            case 30:
            case 35:
                setStatus(QStringLiteral("Timed out (code %1)").arg(code));
                break;
            case 255:
                setStatus(QStringLiteral("SSH connection failed (code 255) — check the host, port, "
                                         "key, or that the host key is trusted"));
                break;
            default:
                setStatus(QStringLiteral("rsync exited with code %1").arg(code));
            }
        }
        m_activeFingerprint.clear();
    });

    connect(m_engine, &SyncEngine::failedToStart, this, [this](const QString &reason) {
        setRunning(false);
        m_activeFingerprint.clear();
        appendLog(QStringLiteral("[error] ") + reason);
        setStatus(QStringLiteral("Failed to start: %1").arg(reason));
    });
}

QString JobController::rsyncSummary() const
{
    if (!m_caps.found)
        return QStringLiteral("No rsync found — install GNU rsync (brew install rsync)");
    QString s = QStringLiteral("rsync %1  ·  %2").arg(m_caps.versionString, m_caps.path);
    if (m_caps.isOpenRsync)
        s += QStringLiteral("  ·  limited; install GNU rsync for live progress");
    return s;
}

QString JobController::endpointKind(const QString &text) const
{
    return EndpointParser::kindName(text);
}

SyncJob JobController::jobFromMap(const QVariantMap &m) const
{
    SyncJob j;
    j.name = m.value(QStringLiteral("name")).toString();
    j.source = m.value(QStringLiteral("source")).toString().trimmed();
    j.destination = m.value(QStringLiteral("destination")).toString().trimmed();
    j.archive = m.value(QStringLiteral("archive"), true).toBool();
    j.compress = m.value(QStringLiteral("compress"), false).toBool();
    j.deleteExtraneous = m.value(QStringLiteral("deleteExtras"), false).toBool();
    j.checksum = m.value(QStringLiteral("checksum"), false).toBool();
    j.maxDelete = qMax(0, m.value(QStringLiteral("maxDelete"), 0).toInt());
    j.excludes = stringListFromVariant(m.value(QStringLiteral("excludes")));
    j.extraArgs = stringListFromVariant(m.value(QStringLiteral("extraArgs")));
    j.sshKeyPath = m.value(QStringLiteral("sshKey")).toString().trimmed();
    j.sshPort = qBound(0, m.value(QStringLiteral("sshPort")).toInt(), 65535);
    j.daemonPassword = m.value(QStringLiteral("daemonPassword")).toString();
    j.sshPassword = m.value(QStringLiteral("sshPassword")).toString();
    j.schedule = scheduleKindFromString(m.value(QStringLiteral("schedule")).toString());
    j.intervalMinutes = qMax(1, m.value(QStringLiteral("intervalMinutes"), 60).toInt());
    j.atHour = qBound(0, m.value(QStringLiteral("atHour"), 9).toInt(), 23);
    j.atMinute = qBound(0, m.value(QStringLiteral("atMinute"), 0).toInt(), 59);
    j.weekday = qBound(0, m.value(QStringLiteral("weekday"), 0).toInt(), 6);
    return j;
}

QVariantMap JobController::mapFromJob(const SyncJob &j) const
{
    return {
        {QStringLiteral("name"), j.name},
        {QStringLiteral("source"), j.source},
        {QStringLiteral("destination"), j.destination},
        {QStringLiteral("archive"), j.archive},
        {QStringLiteral("compress"), j.compress},
        {QStringLiteral("deleteExtras"), j.deleteExtraneous},
        {QStringLiteral("checksum"), j.checksum},
        {QStringLiteral("maxDelete"), j.maxDelete},
        {QStringLiteral("excludes"), j.excludes},
        {QStringLiteral("extraArgs"), j.extraArgs},
        {QStringLiteral("sshKey"), j.sshKeyPath},
        {QStringLiteral("sshPort"), j.sshPort},
        {QStringLiteral("schedule"), scheduleKindToString(j.schedule)},
        {QStringLiteral("intervalMinutes"), j.intervalMinutes},
        {QStringLiteral("atHour"), j.atHour},
        {QStringLiteral("atMinute"), j.atMinute},
        {QStringLiteral("weekday"), j.weekday},
        // daemonPassword is session-only and never pushed back into the editor.
    };
}

void JobController::preview(const QVariantMap &job)
{
    startJob(jobFromMap(job), /*dryRun=*/true);
}

void JobController::run(const QVariantMap &job)
{
    startJob(jobFromMap(job), /*dryRun=*/false);
}

void JobController::startJob(const SyncJob &job, bool dryRun)
{
    if (m_running)
        return;
    if (job.source.isEmpty() || job.destination.isEmpty()) {
        setStatus(QStringLiteral("Set both a source and a destination"));
        return;
    }
    if (!m_caps.found) {
        setStatus(QStringLiteral("No rsync binary available"));
        return;
    }

    m_activeDryRun = dryRun;

    SyncJob j = job;
    // For a daemon target with no password typed this session, pull the stored one
    // from the keychain — but only if the edited endpoint still matches the saved
    // job, so we never send one server's password to a newly-typed host.
    if (j.daemonPassword.isEmpty() && EndpointParser::usesDaemon(j) && !m_currentId.isEmpty()) {
        const SyncJob saved = m_jobs.jobById(m_currentId);
        if (saved.source == j.source && saved.destination == j.destination)
            j.daemonPassword = m_secrets.get(m_currentId);
    }
    // Likewise pull a remembered SSH password (stored under "<id>.ssh") for an
    // unchanged SSH target, so a saved credential authenticates without re-prompting.
    if (j.sshPassword.isEmpty() && EndpointParser::usesSsh(j) && !m_currentId.isEmpty()) {
        const SyncJob saved = m_jobs.jobById(m_currentId);
        if (saved.source == j.source && saved.destination == j.destination)
            j.sshPassword = m_secrets.get(m_currentId + QStringLiteral(".ssh"));
    }
    if (EndpointParser::usesSsh(j)) {
        const Endpoint e = EndpointParser::isSsh(j.source) ? EndpointParser::parse(j.source)
                                                           : EndpointParser::parse(j.destination);
        const SshHost savedHost = m_sshHostStore.load(e.sshTarget);
        if (!savedHost.target.isEmpty()) {
            if (j.sshKeyPath.isEmpty())
                j.sshKeyPath = savedHost.sshKeyPath;
            if (j.sshPort == 0)
                j.sshPort = savedHost.sshPort;
            if (j.sshPassword.isEmpty() && savedHost.hasPassword)
                j.sshPassword = m_secrets.get(sshHostSecretKey(savedHost.target));
        }
    }

    m_activeUsedPassword = !j.sshPassword.isEmpty();
    m_activeRemote = EndpointParser::isSsh(j.source) ? j.source
        : (EndpointParser::isSsh(j.destination) ? j.destination : QString());

    const QByteArray fingerprint = syncFingerprint(j);
    if (!dryRun && j.deleteExtraneous && fingerprint != m_lastPreviewFingerprint) {
        setStatus(QStringLiteral("Preview this exact delete sync before running"));
        return;
    }

    m_activeFingerprint = fingerprint;
    m_engine->start(j, dryRun);
}

void JobController::cancel()
{
    m_engine->cancel();
}

void JobController::retryWithPassword(const QVariantMap &job, const QString &username,
                                      const QString &password, bool remember)
{
    if (password.isEmpty())
        return;

    SyncJob j = jobFromMap(job);
    // Fold the username into whichever endpoint is the SSH target.
    if (EndpointParser::isSsh(j.source))
        j.source = EndpointParser::withUser(j.source, username);
    else if (EndpointParser::isSsh(j.destination))
        j.destination = EndpointParser::withUser(j.destination, username);
    j.sshPassword = password;

    // Persist for saved jobs as before, and also save the SSH target itself so an
    // unsaved/manual credential can appear in the sidebar and be reused later.
    if (remember) {
        saveSshHost(j, true);
        const SshHost host = sshHostFromJob(j, true);
        if (!host.target.isEmpty())
            m_secrets.set(sshHostSecretKey(host.target), password);
        if (!m_currentId.isEmpty())
            m_secrets.set(m_currentId + QStringLiteral(".ssh"), password);
    }

    startJob(j, m_activeDryRun);  // repeat the failed run in the same mode
}

void JobController::newJob()
{
    m_currentId.clear();
    emit currentChanged();
    emit jobLoaded(mapFromJob(SyncJob{}));  // blank, archive on by default
}

void JobController::loadJob(const QString &id)
{
    const SyncJob j = m_jobs.jobById(id);
    if (j.id.isEmpty())
        return;
    m_currentId = j.id;
    emit currentChanged();
    emit jobLoaded(mapFromJob(j));
}

void JobController::saveJob(const QVariantMap &jobMap)
{
    SyncJob job = jobFromMap(jobMap);
    job.id = m_currentId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : m_currentId;
    if (job.name.trimmed().isEmpty())
        job.name = QStringLiteral("Untitled sync");
    else
        job.name = job.name.trimmed();

    if (!m_store.save(job)) {
        setStatus(QStringLiteral("Could not save job"));
        return;
    }
    m_jobs.upsert(job);
    rebuildSshHosts();
    if (!job.daemonPassword.isEmpty())
        m_secrets.set(job.id, job.daemonPassword);  // keychain, never the profile JSON
    else if (!EndpointParser::usesDaemon(job))
        m_secrets.remove(job.id);  // target no longer a daemon -> drop any stale secret

    // SSH password (stored under "<id>.ssh") is remembered only when the user opted in
    // via the auth modal; otherwise drop any stale secret if the target isn't SSH.
    const QString sshKey = job.id + QStringLiteral(".ssh");
    if (jobMap.value(QStringLiteral("rememberSshPassword")).toBool() && !job.sshPassword.isEmpty())
        m_secrets.set(sshKey, job.sshPassword);
    else if (!EndpointParser::usesSsh(job))
        m_secrets.remove(sshKey);
    m_currentId = job.id;
    emit currentChanged();

    m_scheduler.apply(job, Scheduler::runnerPath());  // registers or unregisters per schedule

    if (job.schedule == ScheduleKind::Manual) {
        setStatus(QStringLiteral("Saved '%1'").arg(job.name));
    } else if (EndpointParser::usesDaemon(job) && m_secrets.get(job.id).isEmpty()) {
        // A scheduled daemon sync with no stored password can't authenticate.
        setStatus(QStringLiteral("Saved '%1' · scheduled — set a daemon password so "
                                 "scheduled runs can authenticate").arg(job.name));
    } else {
        setStatus(QStringLiteral("Saved '%1' · scheduled").arg(job.name));
    }
}

QString JobController::sshTargetForJob(const QVariantMap &jobMap) const
{
    const SshHost host = sshHostFromJob(jobFromMap(jobMap), false);
    return host.target;
}

bool JobController::isSshHostSaved(const QString &target) const
{
    return !target.trimmed().isEmpty() && m_sshHostStore.contains(target.trimmed());
}

void JobController::saveSshHostForJob(const QVariantMap &jobMap)
{
    saveSshHost(jobFromMap(jobMap), false);
}

void JobController::saveSshHostPassword(const QString &endpoint, const QString &username,
                                        const QString &password, const QString &sshKey,
                                        int sshPort)
{
    if (password.isEmpty())
        return;

    SyncJob job;
    job.destination = EndpointParser::withUser(endpoint, username);
    job.sshKeyPath = sshKey.trimmed();
    job.sshPort = qBound(0, sshPort, 65535);
    saveSshHost(job, true);

    const SshHost host = sshHostFromJob(job, true);
    if (!host.target.isEmpty())
        m_secrets.set(sshHostSecretKey(host.target), password);
}

void JobController::deleteJob(const QString &id)
{
    m_scheduler.remove(id);  // drop any OS schedule first
    m_secrets.remove(id);    // and its stored daemon password
    m_secrets.remove(id + QStringLiteral(".ssh"));  // and any remembered SSH password
    m_store.remove(id);
    m_jobs.removeById(id);
    rebuildSshHosts();
    if (m_currentId == id)
        newJob();
}

void JobController::setDiscoverable(bool on)
{
    if (m_discoverable == on)
        return;
    m_discoverable = on;
    if (m_discovery)
        m_discovery->setAdvertising(on);
    emit discoverableChanged();
}

void JobController::rebuildSshHosts()
{
    QList<SyncJob> jobs;
    for (int row = 0; row < m_jobs.rowCount(); ++row) {
        const QModelIndex idx = m_jobs.index(row);
        const QString id = m_jobs.data(idx, JobListModel::IdRole).toString();
        const SyncJob job = m_jobs.jobById(id);
        if (!job.id.isEmpty())
            jobs << job;
    }
    m_sshHosts.setHosts(m_sshHostStore.loadAll(), jobs);
}

QString JobController::sshHostSecretKey(const QString &target) const
{
    return QStringLiteral("ssh-host:") + target;
}

SshHost JobController::sshHostFromJob(const SyncJob &job, bool hasPassword) const
{
    const Endpoint e = EndpointParser::isSsh(job.source) ? EndpointParser::parse(job.source)
        : (EndpointParser::isSsh(job.destination) ? EndpointParser::parse(job.destination)
                                                  : Endpoint{});
    if (e.kind != EndpointKind::Ssh || e.sshTarget.isEmpty())
        return {};

    SshHost host;
    host.target = e.sshTarget;
    const int at = e.sshTarget.lastIndexOf(QLatin1Char('@'));
    host.user = at >= 0 ? e.sshTarget.left(at) : QString();
    host.host = at >= 0 ? e.sshTarget.mid(at + 1) : e.sshTarget;
    host.label = e.sshTarget;
    host.sshKeyPath = job.sshKeyPath;
    host.sshPort = job.sshPort;
    host.hasPassword = hasPassword;
    return host;
}

void JobController::saveSshHost(const SyncJob &job, bool hasPassword)
{
    SshHost host = sshHostFromJob(job, hasPassword);
    if (host.target.isEmpty())
        return;

    const SshHost existing = m_sshHostStore.load(host.target);
    if (!existing.target.isEmpty()) {
        if (host.sshKeyPath.isEmpty())
            host.sshKeyPath = existing.sshKeyPath;
        if (host.sshPort == 0)
            host.sshPort = existing.sshPort;
        host.hasPassword = hasPassword || existing.hasPassword;
    }
    if (m_sshHostStore.upsert(host))
        rebuildSshHosts();
}

void JobController::addPeerByHost(const QString &host)
{
    if (m_discovery)
        m_discovery->addManualPeer(host);
}

void JobController::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

void JobController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void JobController::appendLog(const QString &line)
{
    constexpr int kMaxLogLines = 1000;
    m_logLines << line;
    while (m_logLines.size() > kMaxLogLines)
        m_logLines.removeFirst();
    emit logChanged();
}

QByteArray JobController::syncFingerprint(const SyncJob &job) const
{
    const auto stringArray = [](const QStringList &values) {
        QJsonArray a;
        for (const QString &value : values)
            a.append(value);
        return a;
    };

    QJsonObject o;
    o[QStringLiteral("source")] = job.source;
    o[QStringLiteral("destination")] = job.destination;
    o[QStringLiteral("archive")] = job.archive;
    o[QStringLiteral("compress")] = job.compress;
    o[QStringLiteral("checksum")] = job.checksum;
    o[QStringLiteral("deleteExtraneous")] = job.deleteExtraneous;
    o[QStringLiteral("maxDelete")] = job.maxDelete;
    o[QStringLiteral("excludes")] = stringArray(job.excludes);
    o[QStringLiteral("extraArgs")] = stringArray(job.extraArgs);
    o[QStringLiteral("sshKeyPath")] = job.sshKeyPath;
    o[QStringLiteral("sshPort")] = job.sshPort;

    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(json, QCryptographicHash::Sha256);
}
