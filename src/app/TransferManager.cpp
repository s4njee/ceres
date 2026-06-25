#include "app/TransferManager.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

#include "engine/RsyncProcessEngine.h"
#include "engine/SyncEngine.h"

namespace {
const QString kHistoryKey = QStringLiteral("transferHistory");
constexpr int kHistoryCap = 200;

QSettings historySettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Ceres"),
                     QStringLiteral("Ceres"));
}
}  // namespace

TransferManager::TransferManager(RsyncCapabilities caps, QObject *parent)
    : QObject(parent)
    // Each admitted transfer gets its own engine; rsync runs are independent so
    // there's no shared state to coordinate between them. Per-file progress is
    // streamed from the real transfer; there is no separate dry-run enumeration.
    , m_factory([caps] {
        auto *e = new RsyncProcessEngine(caps, nullptr);
        e->setPerFileProgress(true);
        return e;
    })
{
}

TransferManager::TransferManager(EngineFactory factory, QObject *parent)
    : QObject(parent)
    , m_factory(std::move(factory))
{
}

void TransferManager::setMaxConcurrent(int n)
{
    if (n < 1)
        n = 1;
    if (n == m_maxConcurrent)
        return;
    m_maxConcurrent = n;
    emit maxConcurrentChanged();
    // Raising the cap may admit more queued transfers right away.
    pump();
}

void TransferManager::setRateLimitKBps(int n)
{
    if (n < 0)
        n = 0;
    if (n == m_rateLimitKBps)
        return;
    m_rateLimitKBps = n;
    emit rateLimitChanged();
}

void TransferManager::setVerifyChecksums(bool on)
{
    if (on == m_verifyChecksums)
        return;
    m_verifyChecksums = on;
    emit verifyChanged();
}

void TransferManager::setOverwritePolicy(int policy)
{
    if (policy < Overwrite || policy > NewerOnly || policy == m_overwritePolicy)
        return;
    m_overwritePolicy = policy;
    emit overwritePolicyChanged();
}

QString TransferManager::enqueue(const SyncJob &job, const QString &direction, const QString &name)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_model.add(id, name, direction, job.source, job.destination);
    m_jobs.insert(id, job);  // retained so retry() can resubmit a failed/cancelled run
    m_queue.append(Pending{id, job});
    emit enqueued();
    pump();
    return id;
}

void TransferManager::pump()
{
    while (m_active.size() < m_maxConcurrent) {
        // Start the first queued transfer that isn't paused.
        int idx = -1;
        for (int i = 0; i < m_queue.size(); ++i) {
            if (!m_paused.contains(m_queue.at(i).id)) {
                idx = i;
                break;
            }
        }
        if (idx < 0)
            break;

        const Pending pending = m_queue.takeAt(idx);
        const QString id = pending.id;
        SyncJob job = pending.job;
        job.bwLimitKBps = m_rateLimitKBps;     // apply the current rate cap at start time
        job.checksum = job.checksum || m_verifyChecksums;  // -c content verification
        job.ignoreExisting = (m_overwritePolicy == SkipExisting);
        job.updateOnly = (m_overwritePolicy == NewerOnly);

        SyncEngine *e = m_factory();
        e->setParent(this);
        m_active.insert(id, e);
        m_model.setStatus(id, TransfersModel::Active);

        connect(e, &SyncEngine::change, this, [this, id](const ChangeItem &c) {
            if (c.op == ChangeItem::Op::Update && c.fileType == QLatin1Char('f'))
                m_model.updateFileProgress(id, c.path, 0, QString());
        });

        connect(e, &SyncEngine::progress, this, [this, id](const ProgressInfo &info) {
            m_model.updateProgress(id, info.percent, info.rate, info.eta);
        });

        // rsync --stats emits several summary lines; the one with the transfer rate is
        // the throughput summary worth keeping ("sent … received … bytes/sec").
        connect(e, &SyncEngine::stats, this, [this, id](const QString &line) {
            if (line.contains(QStringLiteral("bytes/sec")))
                m_model.setSummary(id, line.trimmed());
        });

        connect(e, &SyncEngine::fileProgress, this,
                [this, id](const QString &path, int percent, const QString &rate) {
            m_model.updateFileProgress(id, path, percent, rate);
        });

        // A crash here means cancel() was requested (the engine reports an
        // interrupted run as crashed=true), so map it to Cancelled; otherwise a
        // zero exit is Done and anything else is Failed. Remove from the active
        // map *before* deleteLater and never touch `e` afterwards.
        connect(e, &SyncEngine::finished, this, [this, id, e](int code, bool crashed) {
            // A clean run transferred every file that needed it; any seeded leaf still
            // at 0% was simply already up-to-date on the far side — flag it as such
            // rather than leaving it looking stalled.
            if (!crashed && code == 0) {
                m_model.markUntouchedUpToDate(id);
                m_jobs.remove(id);  // succeeded — nothing to retry
            }
            m_model.setStatus(id, crashed ? TransfersModel::Cancelled
                                          : (code == 0 ? TransfersModel::Done
                                                       : TransfersModel::Failed));
            if (crashed || code != 0)
                ++m_batchFailures;
            recordHistory(id, crashed ? QStringLiteral("Cancelled")
                                      : (code == 0 ? QStringLiteral("Done")
                                                   : QStringLiteral("Failed")));
            emit transferFinished(id, !crashed && code == 0);
            m_active.remove(id);
            m_paused.remove(id);
            e->deleteLater();
            emit activeCountChanged();
            pump();
            notifyIfDrained();
        });

        connect(e, &SyncEngine::failedToStart, this, [this, id, e](const QString &reason) {
            m_model.setStatus(id, TransfersModel::Failed, reason);
            recordHistory(id, QStringLiteral("Failed"));
            ++m_batchFailures;
            m_active.remove(id);
            m_paused.remove(id);
            e->deleteLater();
            emit activeCountChanged();
            pump();
            notifyIfDrained();
        });

        emit activeCountChanged();
        e->start(job, /*dryRun=*/false);
    }
}

void TransferManager::recordHistory(const QString &id, const QString &status)
{
    // Pull display fields from the live row (still present until clearCompleted).
    QString name, direction, dest;
    for (int i = 0; i < m_model.rowCount(); ++i) {
        const QModelIndex mi = m_model.index(i);
        if (m_model.data(mi, TransfersModel::IdRole).toString() == id) {
            name = m_model.data(mi, TransfersModel::NameRole).toString();
            direction = m_model.data(mi, TransfersModel::DirectionRole).toString();
            dest = m_model.data(mi, TransfersModel::DestinationRole).toString();
            break;
        }
    }
    if (name.isEmpty())
        return;

    QSettings s = historySettings();
    QJsonArray arr = QJsonDocument::fromJson(s.value(kHistoryKey).toByteArray()).array();
    QJsonObject o;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("direction")] = direction;
    o[QStringLiteral("destination")] = dest;
    o[QStringLiteral("status")] = status;
    o[QStringLiteral("time")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    arr.prepend(o);  // most-recent first
    while (arr.size() > kHistoryCap)
        arr.removeLast();
    s.setValue(kHistoryKey, QJsonDocument(arr).toJson(QJsonDocument::Compact));
    emit historyChanged();
}

QVariantList TransferManager::history() const
{
    QSettings s = historySettings();
    const QJsonArray arr = QJsonDocument::fromJson(s.value(kHistoryKey).toByteArray()).array();
    QVariantList out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out << v.toObject().toVariantMap();
    return out;
}

void TransferManager::clearHistory()
{
    QSettings s = historySettings();
    s.remove(kHistoryKey);
    emit historyChanged();
}

void TransferManager::notifyIfDrained()
{
    // Paused-but-active transfers still occupy m_active, so a queue with only paused
    // work isn't "drained"; the check naturally waits for them to resume and finish.
    if (!m_active.isEmpty() || !m_queue.isEmpty())
        return;
    const int failed = m_batchFailures;
    m_batchFailures = 0;
    emit allTransfersComplete(failed);
}

void TransferManager::cancel(const QString &id)
{
    m_paused.remove(id);
    if (SyncEngine *e = m_active.value(id, nullptr)) {
        // The finished() handler will mark the row Cancelled (crashed=true). cancel()
        // continues a paused group first, so the terminate is actually delivered.
        e->cancel();
        return;
    }

    // Still queued: drop it before it ever starts and mark it Cancelled.
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).id == id) {
            m_queue.removeAt(i);
            m_model.setStatus(id, TransfersModel::Cancelled);
            recordHistory(id, QStringLiteral("Cancelled"));
            return;
        }
    }
}

void TransferManager::retry(const QString &id)
{
    // Only terminal-but-unsuccessful transfers can be retried: it must not be running,
    // already queued, or unknown (a succeeded run drops its retained job).
    if (m_active.contains(id))
        return;
    const auto it = m_jobs.constFind(id);
    if (it == m_jobs.cend())
        return;
    for (const Pending &p : m_queue) {
        if (p.id == id)
            return;  // already waiting in the queue
    }

    m_paused.remove(id);
    m_model.setStatus(id, TransfersModel::Queued);  // also clears the error text
    m_queue.append(Pending{id, it.value()});
    pump();
}

void TransferManager::pause(const QString &id)
{
    if (m_paused.contains(id))
        return;

    if (SyncEngine *e = m_active.value(id, nullptr)) {
        // Active: suspend in place. It keeps its slot, so the queue doesn't advance.
        m_paused.insert(id);
        e->pause();
        m_model.setStatus(id, TransfersModel::Paused);
        return;
    }
    // Queued: hold it out of pump() until resumed.
    for (const Pending &p : m_queue) {
        if (p.id == id) {
            m_paused.insert(id);
            m_model.setStatus(id, TransfersModel::Paused);
            return;
        }
    }
}

void TransferManager::resume(const QString &id)
{
    if (!m_paused.contains(id))
        return;
    m_paused.remove(id);

    if (SyncEngine *e = m_active.value(id, nullptr)) {
        e->resume();
        m_model.setStatus(id, TransfersModel::Active);
        return;
    }
    // Was a paused queued entry — back to Queued and let pump() pick it up.
    m_model.setStatus(id, TransfersModel::Queued);
    pump();
}
