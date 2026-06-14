#pragma once

#include <QProcess>

#include "engine/BinaryLocator.h"
#include "engine/OutputParser.h"
#include "engine/SyncEngine.h"

// Drives the real rsync binary via QProcess, fully async (never blocks the GUI
// thread). On Unix the child is placed in its own process group so cancel()
// can signal the whole group — otherwise an `-e ssh` child would be orphaned.
class RsyncProcessEngine : public SyncEngine {
    Q_OBJECT
public:
    explicit RsyncProcessEngine(RsyncCapabilities caps, QObject *parent = nullptr);

    void start(const SyncJob &job, bool dryRun) override;
    void cancel() override;
    bool isRunning() const override;

    const RsyncCapabilities &capabilities() const { return m_caps; }

private:
    void ensureProcess();

    RsyncCapabilities m_caps;
    QProcess *m_process = nullptr;
    OutputParser m_parser;
};
