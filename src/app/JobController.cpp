#include "app/JobController.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QSysInfo>
#include <QUuid>

#include "core/Peer.h"
#include "core/SyncJob.h"
#include "engine/RsyncProcessEngine.h"
#include "net/DiscoveryService.h"

namespace {
// Best-effort primary LAN IPv4: prefer a physical interface (en0) over virtual
// ones (utun/awdl/Tailscale), which we keep only as a fallback.
QString detectPrimaryAddress()
{
    QString fallback;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning))
            continue;
        if (flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QString name = iface.name();
        const bool virtualish = name.startsWith(QLatin1String("utun"))
            || name.startsWith(QLatin1String("awdl")) || name.startsWith(QLatin1String("llw"))
            || name.startsWith(QLatin1String("bridge"));
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback())
                continue;
            if (virtualish) {
                if (fallback.isEmpty())
                    fallback = ip.toString();
                continue;
            }
            return ip.toString();
        }
    }
    return fallback;
}

// All non-loopback IPv4 addresses, physical interfaces first, virtual/overlay
// (utun/awdl/Tailscale) after — so the beacon advertises every reachable route.
QStringList gatherAddresses()
{
    QStringList physical;
    QStringList virtualish;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto f = iface.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) || !f.testFlag(QNetworkInterface::IsRunning)
            || f.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QString name = iface.name();
        const bool v = name.startsWith(QLatin1String("utun")) || name.startsWith(QLatin1String("awdl"))
            || name.startsWith(QLatin1String("llw")) || name.startsWith(QLatin1String("bridge"));
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback())
                continue;
            (v ? virtualish : physical) << ip.toString();
        }
    }
    return physical + virtualish;
}

QString osName()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#else
    return QStringLiteral("Linux");
#endif
}
} // namespace

JobController::JobController(QObject *parent)
    : QObject(parent), m_caps(BinaryLocator::locateRsync())
{
    m_hostName = QHostInfo::localHostName();
    m_hostAddress = detectPrimaryAddress();

    const QList<SyncJob> loaded = m_store.loadAll();
    m_jobs.setJobs(loaded);
    // Re-register only schedules whose OS unit went missing (e.g. deleted out of
    // band). Re-applying an already-registered job would reload its launchd agent
    // and reset a StartInterval countdown on every launch; saveJob() already
    // re-applies when a schedule is edited.
    for (const SyncJob &j : loaded) {
        if (j.schedule != ScheduleKind::Manual && !m_scheduler.isRegistered(j.id))
            m_scheduler.apply(j, Scheduler::runnerPath());
    }

    Peer self;
    self.id = qEnvironmentVariable("CERES_NODE_ID");  // override lets two dev instances coexist
    if (self.id.isEmpty())
        self.id = QString::fromLatin1(QSysInfo::machineUniqueId());
    if (self.id.isEmpty())
        self.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    self.name = m_hostName;
    self.addresses = gatherAddresses();
    self.os = osName();
    self.version = QStringLiteral("0.1");
    self.accepts = {QStringLiteral("ssh")};
    m_discovery = new DiscoveryService(&m_peers, self, this);
    m_discovery->start();

    m_engine = new RsyncProcessEngine(m_caps, this);

    connect(m_engine, &SyncEngine::change, &m_changes, &ChangeListModel::append);

    connect(m_engine, &SyncEngine::progress, this, [this](const ProgressInfo &p) {
        if (m_percent != p.percent) {
            m_percent = p.percent;
            emit progressChanged();
        }
    });

    connect(m_engine, &SyncEngine::log, this, [this](const QString &l) { appendLog(l); });
    connect(m_engine, &SyncEngine::stats, this, [this](const QString &l) { appendLog(l); });
    connect(m_engine, &SyncEngine::errorOutput, this,
            [this](const QString &l) { appendLog(QStringLiteral("[stderr] ") + l); });

    connect(m_engine, &SyncEngine::started, this, [this] {
        m_changes.clear();
        m_log.clear();
        emit logChanged();
        m_percent = 0;
        emit progressChanged();
        setRunning(true);
        setStatus(QStringLiteral("Scanning…"));
    });

    connect(m_engine, &SyncEngine::finished, this, [this](int code, bool crashed) {
        setRunning(false);
        if (crashed) {
            setStatus(QStringLiteral("Cancelled / interrupted"));
        } else if (code == 0) {
            if (m_activeDryRun) {
                setStatus(QStringLiteral("Preview complete — %1 change(s)").arg(m_changes.rowCount()));
            } else {
                m_percent = 100;
                emit progressChanged();
                setStatus(QStringLiteral("Sync complete — %1 item(s)").arg(m_changes.rowCount()));
            }
        } else {
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
    });

    connect(m_engine, &SyncEngine::failedToStart, this, [this](const QString &reason) {
        setRunning(false);
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
    j.sshKeyPath = m.value(QStringLiteral("sshKey")).toString().trimmed();
    j.sshPort = m.value(QStringLiteral("sshPort")).toInt();
    j.daemonPassword = m.value(QStringLiteral("daemonPassword")).toString();
    j.schedule = scheduleKindFromString(m.value(QStringLiteral("schedule")).toString());
    j.intervalMinutes = m.value(QStringLiteral("intervalMinutes"), 60).toInt();
    j.atHour = m.value(QStringLiteral("atHour"), 9).toInt();
    j.atMinute = m.value(QStringLiteral("atMinute"), 0).toInt();
    j.weekday = m.value(QStringLiteral("weekday"), 0).toInt();
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
    m_engine->start(job, dryRun);
}

void JobController::cancel()
{
    m_engine->cancel();
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
    m_currentId = job.id;
    emit currentChanged();

    m_scheduler.apply(job, Scheduler::runnerPath());  // registers or unregisters per schedule

    if (job.schedule == ScheduleKind::Manual) {
        setStatus(QStringLiteral("Saved '%1'").arg(job.name));
    } else {
        // The daemon password is session-only (never persisted), so a scheduled
        // rsync:// run can't authenticate — warn rather than silently mislead.
        const auto isDaemon = [](const QString &p) {
            return p.startsWith(QStringLiteral("rsync://")) || p.contains(QStringLiteral("::"));
        };
        if (isDaemon(job.source) || isDaemon(job.destination))
            setStatus(QStringLiteral("Saved '%1' · scheduled — note: daemon password isn't saved, "
                                     "scheduled runs need an SSH target").arg(job.name));
        else
            setStatus(QStringLiteral("Saved '%1' · scheduled").arg(job.name));
    }
}

void JobController::deleteJob(const QString &id)
{
    m_scheduler.remove(id);  // drop any OS schedule first
    m_store.remove(id);
    m_jobs.removeById(id);
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
    if (!m_log.isEmpty())
        m_log += QLatin1Char('\n');
    m_log += line;
    emit logChanged();
}
