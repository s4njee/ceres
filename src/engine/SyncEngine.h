#pragma once

#include <QObject>

#include "core/SyncJob.h"
#include "engine/RsyncEvents.h"

/// Abstract transport-agnostic sync engine — the **portability seam** in Ceres.
///
/// The GUI, models, and controller only ever talk to this interface, never to
/// QProcess or rsync directly. This means a future Windows engine (cwRsync or
/// WSL-backed) can implement `start()`/`cancel()`/`isRunning()` behind the same
/// contract without touching anything above this layer.
///
/// **Signal contract**: every engine must emit `started()` when a run begins,
/// stream `change()`/`progress()`/`stats()`/`log()` during the run, and always
/// emit exactly one `finished(exitCode, crashed)` when it ends. The controller
/// relies on this lifecycle to manage its UI state machine.
///
/// Currently the only concrete implementation is RsyncProcessEngine, which
/// drives the real rsync binary via QProcess.
/// @ingroup engine
class SyncEngine : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~SyncEngine() override = default;

    virtual void start(const SyncJob &job, bool dryRun) = 0;
    virtual void cancel() = 0;
    virtual bool isRunning() const = 0;

signals:
    void change(const ChangeItem &item);
    void progress(const ProgressInfo &info);
    void stats(const QString &line);
    void log(const QString &line);
    void errorOutput(const QString &line);
    void started();
    void finished(int exitCode, bool crashed);
    void failedToStart(const QString &reason);
};
