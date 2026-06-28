#include "app/JobController.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <utility>

#include "core/Endpoint.h"
#include "core/PairingCode.h"
#include "core/Peer.h"
#include "core/SshConfigImport.h"
#include "core/SshKnownHosts.h"
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

QString formatBytes(qint64 bytes)
{
    if (bytes < 0)
        bytes = 0;

    static const QStringList units = {QStringLiteral("B"), QStringLiteral("kB"),
                                     QStringLiteral("MB"), QStringLiteral("GB")};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }

    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);

    const int decimals = value < 10.0 ? 1 : 0;
    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', decimals), units.at(unit));
}

QString formatBytesProgress(const ProgressInfo &p)
{
    if (p.bytes <= 0 && p.percent <= 0)
        return {};

    const QString transferred = formatBytes(p.bytes);
    if (p.percent <= 0)
        return transferred + QStringLiteral(" / ?");

    const qint64 total = p.percent >= 100
        ? p.bytes
        : static_cast<qint64>(static_cast<double>(p.bytes) * 100.0 / p.percent + 0.5);
    return transferred + QStringLiteral(" / ") + formatBytes(total);
}

} // namespace

JobController::JobController(QObject *parent)
    : JobController(BinaryLocator::locateRsync(), nullptr, SecretStore{},
                    SshHostStore{}, true, parent)
{
}

JobController::JobController(RsyncCapabilities caps, SyncEngine *engine,
                             SecretStore secrets, SshHostStore sshHostStore,
                             bool startNetworkServices,
                             QObject *parent)
    : QObject(parent),
      m_caps(std::move(caps)),
      m_secrets(std::move(secrets)),
      m_sshHostStore(std::move(sshHostStore))
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

    m_sshHosts.setHosts(m_sshHostStore.loadAll());

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
        m_selfId = self.id;
        m_discovery = new DiscoveryService(&m_peers, self, this);
        m_discovery->start();
    }
    refreshPairedIds();  // mark already-paired devices in the sidebar

    m_engine = engine ? engine : new RsyncProcessEngine(m_caps, this);

    connect(m_engine, &SyncEngine::change, &m_changes, &ChangeListModel::append);

    connect(m_engine, &SyncEngine::progress, this, [this](const ProgressInfo &p) {
        if (!m_activeDryRun)
            setStatus(QStringLiteral("Transferring…"));
        const QString bytesProgress = formatBytesProgress(p);
        const bool changed = m_percent != p.percent || m_speed != p.rate
            || m_bytesProgress != bytesProgress;
        if (changed) {
            m_percent = p.percent;
            m_speed = p.rate;
            m_bytesProgress = bytesProgress;
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
        m_bytesProgress.clear();
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

            if (SshKnownHosts::looksLikeChangedHostKey(m_runStderr) && !m_activeRemote.isEmpty()) {
                const Endpoint e = EndpointParser::parse(m_activeRemote);
                const QString host = SshKnownHosts::hostFromTarget(e.sshTarget);
                setStatus(QStringLiteral("Host key changed for %1").arg(host));
                m_activeFingerprint.clear();
                emit sshHostKeyChanged(host);
                return;
            }

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
    return j;
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
    // For an SSH target that matches a saved host, fall back to that host's stored
    // key/port and remembered password so a saved credential authenticates without
    // re-prompting.
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
    m_activeSshPort = j.sshPort;

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

    // Save the SSH target itself so the credential appears in the sidebar and can
    // be reused later.
    if (remember) {
        saveSshHost(j, true);
        const SshHost host = sshHostFromJob(j, true);
        if (!host.target.isEmpty())
            m_secrets.set(sshHostSecretKey(host.target), password);
    }

    startJob(j, m_activeDryRun);  // repeat the failed run in the same mode
}

void JobController::repairKnownHostAndRetry(const QVariantMap &job)
{
    if (m_activeRemote.isEmpty())
        return;

    const Endpoint e = EndpointParser::parse(m_activeRemote);
    const KnownHostRepairResult result =
        SshKnownHosts::removeHost(m_caps, e.sshTarget, m_activeSshPort);
    if (!result.ok) {
        setStatus(result.message);
        return;
    }

    setStatus(result.message);
    startJob(jobFromMap(job), m_activeDryRun);
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
    m_sshHosts.setHosts(m_sshHostStore.loadAll());
}

int JobController::importSshConfig()
{
    QFile f(QDir::homePath() + QStringLiteral("/.ssh/config"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    const QList<SshHost> hosts = SshConfigImport::parse(QString::fromUtf8(f.readAll()));
    int added = 0;
    for (const SshHost &host : hosts) {
        // Skip incomplete entries and anything we already have (don't clobber a saved
        // host's label/key/password with config-derived values).
        if (host.target.isEmpty() || host.host.isEmpty() || m_sshHostStore.contains(host.target))
            continue;
        if (m_sshHostStore.upsert(host))
            ++added;
    }
    if (added > 0)
        rebuildSshHosts();
    return added;
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
    // A friendly label if the caller supplied one (SyncJob.name), else the target.
    host.label = job.name.trimmed().isEmpty() ? e.sshTarget : job.name.trimmed();
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
        // Don't clobber a previously-set friendly label with the default (the target).
        if (host.label == host.target && existing.label != existing.target)
            host.label = existing.label;
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

void JobController::refreshPairedIds()
{
    QSet<QString> ids;
    for (const PairedDevice &d : m_pairedStore.loadAll())
        ids.insert(d.deviceId);
    m_peers.setPairedIds(std::move(ids));
}

QString JobController::pairingCodeFor(const QString &peerId) const
{
    return PairingCode::forDevices(m_selfId, peerId);
}

void JobController::pairPeer(const QString &peerId)
{
    const Peer peer = m_peers.peerById(peerId);
    if (peer.id.isEmpty())
        return;
    PairedDevice d;
    d.deviceId = peer.id;
    d.name = peer.name;
    d.sshTarget = peer.primaryAddress();  // bare host; the connect flow prompts for login
    d.pairedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_pairedStore.upsert(d);
    refreshPairedIds();
}

void JobController::unpairPeer(const QString &peerId)
{
    m_pairedStore.remove(peerId);
    refreshPairedIds();
}

bool JobController::isPaired(const QString &peerId) const
{
    return m_pairedStore.contains(peerId);
}

QString JobController::pairedTarget(const QString &peerId) const
{
    return m_pairedStore.load(peerId).sshTarget;
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
