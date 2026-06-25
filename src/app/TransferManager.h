#pragma once

#include <functional>

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
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
    Q_PROPERTY(int rateLimitKBps READ rateLimitKBps WRITE setRateLimitKBps NOTIFY rateLimitChanged)
    Q_PROPERTY(bool verifyChecksums READ verifyChecksums WRITE setVerifyChecksums NOTIFY verifyChanged)
    Q_PROPERTY(int overwritePolicy READ overwritePolicy WRITE setOverwritePolicy NOTIFY overwritePolicyChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
public:
    using EngineFactory = std::function<SyncEngine *()>;

    // How a transfer treats files already present at the destination.
    enum OverwritePolicy { Overwrite = 0, SkipExisting, NewerOnly };
    Q_ENUM(OverwritePolicy)

    // Production ctor: the default factory creates a fresh RsyncProcessEngine
    // (with these caps) for every admitted transfer.
    explicit TransferManager(RsyncCapabilities caps, QObject *parent = nullptr);
    // Test ctor: inject a factory that hands out fake engines.
    explicit TransferManager(EngineFactory factory, QObject *parent = nullptr);

    TransfersModel *model() { return &m_model; }

    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int n);

    // Transfer-rate cap in KB/s applied to every admitted transfer (0 = unlimited).
    // Stamped onto each job as it starts, so changing it affects newly started
    // transfers, not ones already running.
    int rateLimitKBps() const { return m_rateLimitKBps; }
    void setRateLimitKBps(int n);

    // When on, transfers compare by content checksum (rsync -c) so files are verified
    // by hash rather than size+mtime. Stamped onto each job as it starts.
    bool verifyChecksums() const { return m_verifyChecksums; }
    void setVerifyChecksums(bool on);

    // Overwrite policy applied to each transfer as it starts (see OverwritePolicy).
    int overwritePolicy() const { return m_overwritePolicy; }
    void setOverwritePolicy(int policy);

    int activeCount() const { return m_active.size(); }

    // Enqueue one ad-hoc transfer. `direction` is "up"/"down", `name` is the
    // display label. Returns the generated transfer id. Emits enqueued() and
    // pumps the queue (so it may start immediately if under the cap).
    QString enqueue(const SyncJob &job, const QString &direction, const QString &name);

    // Seed a transfer's file tree with the full list of relative paths it will touch
    // (from a source-side directory walk), each shown at 0% until the live run fills
    // in progress. Safe to call after the transfer has already started; the model
    // skips paths a live update has already created.
    void seedFiles(const QString &id, const QStringList &paths) { m_model.seedFiles(id, paths); }

    Q_INVOKABLE void cancel(const QString &id);
    // Re-run a finished-but-unsuccessful transfer (Failed/Cancelled) under its existing
    // row. With --partial the rsync resumes from the partial file. No-op otherwise.
    Q_INVOKABLE void retry(const QString &id);
    // Suspend/resume a transfer. An active transfer is stopped in place (keeps its
    // slot); a queued one is held out of pump() until resumed.
    Q_INVOKABLE void pause(const QString &id);
    Q_INVOKABLE void resume(const QString &id);
    Q_INVOKABLE void clearCompleted() { m_model.clearCompleted(); }

    // Persistent log of finished transfers (most-recent first), survives restarts.
    // Each entry is a map: { name, direction, destination, status, time }.
    Q_INVOKABLE QVariantList history() const;
    Q_INVOKABLE void clearHistory();

signals:
    void maxConcurrentChanged();
    void rateLimitChanged();
    void verifyChanged();
    void overwritePolicyChanged();
    void activeCountChanged();
    void enqueued();  // emitted by enqueue() so the UI can auto-open the transfers modal
    // The last in-flight transfer finished and nothing is queued: the batch is done.
    // `failed` is how many of the just-drained batch ended in failure/cancellation.
    void allTransfersComplete(int failed);
    void historyChanged();
    // A single transfer reached a terminal state. Lets a caller act on the result of a
    // specific enqueue() (e.g. open a just-downloaded file).
    void transferFinished(const QString &id, bool success);

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
    QList<Pending> m_queue;                 // FIFO; paused entries are skipped by pump()
    QHash<QString, SyncEngine *> m_active;  // id -> running engine (incl. paused ones)
    QHash<QString, SyncJob> m_jobs;         // id -> job, retained so retry() can resubmit
    QSet<QString> m_paused;                 // ids the user paused (queued or active)
    int m_maxConcurrent = 3;
    int m_rateLimitKBps = 0;
    bool m_verifyChecksums = false;
    int m_overwritePolicy = NewerOnly;  // default: only replace when the source is newer
    int m_batchFailures = 0;  // failed/cancelled runs since the queue was last empty

    // Emit allTransfersComplete() when a finish leaves nothing active or queued.
    void notifyIfDrained();
    // Append a finished transfer to the persistent history (capped, most-recent first).
    void recordHistory(const QString &id, const QString &status);
};
