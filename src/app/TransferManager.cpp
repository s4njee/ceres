#include "app/TransferManager.h"

#include <QUuid>

#include "engine/RsyncProcessEngine.h"
#include "engine/SyncEngine.h"

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

QString TransferManager::enqueue(const SyncJob &job, const QString &direction, const QString &name)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_model.add(id, name, direction, job.source, job.destination);
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
        const SyncJob job = pending.job;

        SyncEngine *e = m_factory();
        e->setParent(this);
        m_active.insert(id, e);
        m_model.setStatus(id, TransfersModel::Active);

        connect(e, &SyncEngine::change, this, [this, id](const ChangeItem &c) {
            if (c.op == ChangeItem::Op::Update && c.fileType == QLatin1Char('f'))
                m_model.updateFileProgress(id, c.path, 0, QString());
        });

        connect(e, &SyncEngine::progress, this, [this, id](const ProgressInfo &info) {
            m_model.updateProgress(id, info.percent, info.rate);
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
            m_model.setStatus(id, crashed ? TransfersModel::Cancelled
                                          : (code == 0 ? TransfersModel::Done
                                                       : TransfersModel::Failed));
            m_active.remove(id);
            m_paused.remove(id);
            e->deleteLater();
            emit activeCountChanged();
            pump();
        });

        connect(e, &SyncEngine::failedToStart, this, [this, id, e](const QString &reason) {
            m_model.setStatus(id, TransfersModel::Failed, reason);
            m_active.remove(id);
            m_paused.remove(id);
            e->deleteLater();
            emit activeCountChanged();
            pump();
        });

        emit activeCountChanged();
        e->start(job, /*dryRun=*/false);
    }
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
            return;
        }
    }
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
