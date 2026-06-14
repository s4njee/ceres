#pragma once

#include <QObject>

#include "core/SyncJob.h"
#include "engine/RsyncEvents.h"

// Abstract transport-agnostic sync engine. The GUI and models only ever talk
// to this interface, so a future CwRsyncEngine / WslEngine can be dropped in
// for the Windows port without touching anything above this seam.
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
