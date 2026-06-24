#pragma once

#include <QProcess>

#include "core/SyncJob.h"
#include "engine/BinaryLocator.h"
#include "engine/OutputParser.h"
#include "engine/SyncEngine.h"

/// Concrete SyncEngine that drives the real rsync binary via QProcess.
///
/// Fully asynchronous — never blocks the GUI thread. The data pipeline is:
///   QProcess stdout → OutputParser::feed() → parsed signals → SyncEngine signals
///   QProcess stderr → errorOutput() signal (shown in the log pane)
///
/// On Unix the child process is placed in its own process group via
/// setChildProcessModifier + setpgid(). This is important because rsync with
/// `-e ssh` spawns ssh as a grandchild — a plain QProcess::terminate() would
/// only signal rsync, orphaning the ssh process. By signaling the whole group
/// with `kill(-pid, SIGTERM)`, both rsync and its ssh child are cleaned up.
///
/// On Windows the equivalent is a Job Object: the child is assigned to a job
/// when it starts, so cancel() can TerminateJobObject() and take down rsync
/// plus any ssh grandchild in one call. The job is created with
/// JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE so the tree is also reaped if the app
/// itself dies.
/// @ingroup engine
class RsyncProcessEngine : public SyncEngine {
    Q_OBJECT
public:
    explicit RsyncProcessEngine(RsyncCapabilities caps, QObject *parent = nullptr);

    void start(const SyncJob &job, bool dryRun) override;
    void cancel() override;
    bool isRunning() const override;
    void pause() override;
    void resume() override;

    const RsyncCapabilities &capabilities() const { return m_caps; }

    /// Run rsync with per-file `--progress` so fileProgress() is emitted per file and
    /// progress() is the parser-derived aggregate. Set by TransferManager for ad-hoc
    /// transfers; the saved-job sync path leaves it off (aggregate progress2).
    void setPerFileProgress(bool on) { m_perFileProgress = on; }

#ifdef Q_OS_WIN
    ~RsyncProcessEngine() override;
#endif

private:
    void ensureProcess();
    void launch(bool dryRun);  // configure + start m_process for one phase

    // Per-file transfers run in two phases on one process: a --dry-run pass that
    // itemizes every file to be transferred (so the UI can pre-list them all up
    // front), then the real transfer. A non-per-file run (the sync tab) is Single.
    enum class Phase { Single, Enumerate, Transfer };

    RsyncCapabilities m_caps;
    bool m_perFileProgress = false;
    Phase m_phase = Phase::Single;
    SyncJob m_job;  // retained across the enumerate→transfer phases
    QProcess *m_process = nullptr;
    OutputParser m_parser;
    // Set by cancel() so finished() reports the run as interrupted regardless of
    // how the OS classifies the kill: Unix SIGTERM surfaces as CrashExit, but a
    // Windows TerminateJobObject is a NormalExit with the exit code we passed.
    bool m_cancelRequested = false;

#ifdef Q_OS_WIN
    void *m_jobObject = nullptr;  // HANDLE; kills the rsync process tree on cancel
#endif
};
