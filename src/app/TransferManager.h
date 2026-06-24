#pragma once

#include <functional>

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>

#include "core/SyncJob.h"
#include "engine/BinaryLocator.h"
#include "models/TransfersModel.h"

class SyncEngine;

/// Parallel-capped queue of ad-hoc transfers.
///
/// Drag-and-drop (and other quick) transfers don't go through the saved-job
/// JobController; they're fire-and-forget SyncJobs the user kicks off in bursts.
/// Running them all at once would saturate the link and spawn a process storm,
/// so TransferManager admits at most `maxConcurrent` at a time and queues the
/// rest, draining the queue as engines finish.
///
/// Each admitted transfer gets its own SyncEngine instance (rsync is stateless
/// per run, so independent engines are safe). The engine is created lazily via
/// an injectable EngineFactory — production hands out RsyncProcessEngine, tests
/// hand out fakes — which keeps this class free of any rsync/QProcess coupling
/// and lets the queue logic be unit-tested headlessly.
///
/// The manager owns the TransfersModel and is its sole writer: it adds a Queued
/// row on enqueue(), flips it to Active when pump() starts the engine, and to
/// Done/Failed/Cancelled from the engine's finished()/failedToStart() handlers.
/// @ingroup app
class TransferManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(TransfersModel *model READ model CONSTANT)
    Q_PROPERTY(int maxConcurrent READ maxConcurrent WRITE setMaxConcurrent NOTIFY maxConcurrentChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
public:
    using EngineFactory = std::function<SyncEngine *()>;

    // Production ctor: the default factory creates a fresh RsyncProcessEngine
    // (with these caps) for every admitted transfer.
    explicit TransferManager(RsyncCapabilities caps, QObject *parent = nullptr);
    // Test ctor: inject a factory that hands out fake engines.
    explicit TransferManager(EngineFactory factory, QObject *parent = nullptr);

    TransfersModel *model() { return &m_model; }

    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int n);

    int activeCount() const { return m_active.size(); }

    // Enqueue one ad-hoc transfer. `direction` is "up"/"down", `name` is the
    // display label. Returns the generated transfer id. Emits enqueued() and
    // pumps the queue (so it may start immediately if under the cap).
    QString enqueue(const SyncJob &job, const QString &direction, const QString &name);

    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void clearCompleted() { m_model.clearCompleted(); }

signals:
    void maxConcurrentChanged();
    void activeCountChanged();
    void enqueued();  // emitted by enqueue() so the UI can auto-open the transfers modal

private:
    // Start queued transfers up to maxConcurrent. Re-entrant: called on every
    // enqueue and again whenever an engine finishes, so the queue keeps draining.
    void pump();

    struct Pending {
        QString id;
        SyncJob job;
    };

    TransfersModel m_model;
    EngineFactory m_factory;
    QQueue<Pending> m_queue;
    QHash<QString, SyncEngine *> m_active;  // id -> running engine
    int m_maxConcurrent = 3;
};
