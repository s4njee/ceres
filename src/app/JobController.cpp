#include "app/JobController.h"

#include "core/SyncJob.h"
#include "engine/RsyncProcessEngine.h"

JobController::JobController(QObject *parent)
    : QObject(parent), m_caps(BinaryLocator::locateRsync())
{
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
        if (crashed)
            setStatus(QStringLiteral("Cancelled / interrupted"));
        else if (code == 0)
            setStatus(QStringLiteral("Preview complete — %1 change(s)").arg(m_changes.rowCount()));
        else
            setStatus(QStringLiteral("rsync exited with code %1").arg(code));
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

void JobController::preview(const QString &source, const QString &destination)
{
    if (m_running)
        return;
    if (source.trimmed().isEmpty() || destination.trimmed().isEmpty()) {
        setStatus(QStringLiteral("Set both a source and a destination"));
        return;
    }
    if (!m_caps.found) {
        setStatus(QStringLiteral("No rsync binary available"));
        return;
    }

    SyncJob job;
    job.source = source.trimmed();
    job.destination = destination.trimmed();
    job.archive = true;

    m_engine->start(job, /*dryRun=*/true);
}

void JobController::cancel()
{
    m_engine->cancel();
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
