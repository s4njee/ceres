#include "engine/RsyncProcessEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStringList>
#include <utility>

#include "engine/ArgvBuilder.h"

#ifdef Q_OS_UNIX
#  include <csignal>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#ifdef Q_OS_WIN
#  include <windows.h>

namespace {
// Suspend or resume every process assigned to `job` (rsync plus its ssh child).
// Windows has no SIGSTOP, so we use ntdll's per-process suspend/resume — an
// undocumented but long-stable API — on each PID in the Job Object. Pause/resume
// calls are balanced 1:1 by TransferManager, so the kernel suspend count stays
// in step. TerminateJobObject still works on a suspended tree, so cancel() needs
// no special-casing.
void suspendResumeJobProcesses(HANDLE job, bool suspend)
{
    if (!job)
        return;

    using ProcFn = LONG(NTAPI *)(HANDLE);
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    static const auto suspendFn =
        reinterpret_cast<ProcFn>(::GetProcAddress(ntdll, "NtSuspendProcess"));
    static const auto resumeFn =
        reinterpret_cast<ProcFn>(::GetProcAddress(ntdll, "NtResumeProcess"));
    const ProcFn fn = suspend ? suspendFn : resumeFn;
    if (!fn)
        return;

    // A transfer spawns only rsync + ssh, but over-allocate generously so the
    // query never truncates the PID list.
    constexpr DWORD kMaxPids = 64;
    BYTE buffer[sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + kMaxPids * sizeof(ULONG_PTR)]{};
    if (!::QueryInformationJobObject(job, JobObjectBasicProcessIdList, buffer, sizeof(buffer),
                                     nullptr))
        return;

    const auto *list = reinterpret_cast<const JOBOBJECT_BASIC_PROCESS_ID_LIST *>(buffer);
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
        if (HANDLE proc = ::OpenProcess(PROCESS_SUSPEND_RESUME, FALSE,
                                        static_cast<DWORD>(list->ProcessIdList[i]))) {
            fn(proc);
            ::CloseHandle(proc);
        }
    }
}
}  // namespace
#endif

RsyncProcessEngine::RsyncProcessEngine(RsyncCapabilities caps, QObject *parent)
    : SyncEngine(parent), m_caps(std::move(caps))
{
    // Forward parser output straight through as engine signals.
    connect(&m_parser, &OutputParser::change, this, &SyncEngine::change);
    connect(&m_parser, &OutputParser::progress, this, &SyncEngine::progress);
    connect(&m_parser, &OutputParser::fileProgress, this, &SyncEngine::fileProgress);
    connect(&m_parser, &OutputParser::stats, this, &SyncEngine::stats);
    connect(&m_parser, &OutputParser::log, this, &SyncEngine::log);
}

void RsyncProcessEngine::ensureProcess()
{
    if (m_process)
        return;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);  // keep errors distinct

    connect(m_process, &QProcess::readyReadStandardOutput, this,
            [this] { m_parser.feed(m_process->readAllStandardOutput()); });

    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        const QString err = QString::fromUtf8(m_process->readAllStandardError());
        const auto lines = err.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            emit errorOutput(line);
    });

    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                m_parser.feed(m_process->readAllStandardOutput());
                m_parser.flush();
                const bool interrupted = status == QProcess::CrashExit || m_cancelRequested;
                emit finished(exitCode, interrupted);
            });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit failedToStart(m_process->errorString());
    });

#ifdef Q_OS_UNIX
    // Become a process-group leader so cancel() can signal the whole group.
    m_process->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif

#ifdef Q_OS_WIN
    // Create the kill-on-close job that the child will be assigned to.
    if (!m_jobObject) {
        m_jobObject = ::CreateJobObjectW(nullptr, nullptr);
        if (m_jobObject) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            ::SetInformationJobObject(static_cast<HANDLE>(m_jobObject),
                                      JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
    }

    // Assign the child to a Job Object as soon as it starts, so cancel() can
    // tear down rsync and any ssh grandchild together. We assign on the
    // started() signal (the earliest point a PID is available) rather than
    // before launch, because QProcess owns the CreateProcess call. The small
    // window before assignment is acceptable: rsync forks ssh only after it has
    // parsed its arguments, by which point the job assignment has landed.
    connect(m_process, &QProcess::started, this, [this] {
        if (!m_jobObject)
            return;
        const qint64 pid = m_process->processId();
        if (pid <= 0)
            return;
        HANDLE proc = ::OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                    static_cast<DWORD>(pid));
        if (proc) {
            ::AssignProcessToJobObject(static_cast<HANDLE>(m_jobObject), proc);
            ::CloseHandle(proc);
        }
    });
#endif
}

void RsyncProcessEngine::start(const SyncJob &job, bool dryRun)
{
    if (isRunning())
        return;
    if (!m_caps.found) {
        emit failedToStart(QStringLiteral("No rsync binary found"));
        return;
    }

    m_job = job;
    m_cancelRequested = false;
    emit started();

    launch(dryRun);
}

void RsyncProcessEngine::launch(bool dryRun)
{
    const SyncJob &job = m_job;

    ensureProcess();
    m_parser.reset();
    m_parser.setPerFileProgress(m_perFileProgress);

    m_process->setProgram(m_caps.path);
    ArgvBuilder::BuildOptions opts;
    opts.dryRun = dryRun;
    opts.perFileProgress = m_perFileProgress;
    m_process->setArguments(ArgvBuilder::build(job, m_caps, opts));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();  // carries SSH_AUTH_SOCK
    if (!job.daemonPassword.isEmpty())
        env.insert(QStringLiteral("RSYNC_PASSWORD"), job.daemonPassword);  // rsync:// daemon auth

    // SSH password auth: ssh can't read /dev/tty under QProcess, so feed the password
    // through OpenSSH's askpass hook. We point SSH_ASKPASS at *this* executable, which
    // short-circuits to printing CERES_SSH_PASSWORD when it sees CERES_ASKPASS (see
    // main.cpp). REQUIRE=force makes ssh use the helper even without
    // a DISPLAY/tty (OpenSSH >= 8.4). The password never touches the argv.
    if (ArgvBuilder::usesSsh(job) && !job.sshPassword.isEmpty()) {
        QString askpass = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
        // The bundled Cygwin/MSYS ssh exec's the helper, so hand it a POSIX-form path.
        askpass = ArgvBuilder::toRsyncLocalPath(askpass, m_caps.pathStyle);
#endif
        env.insert(QStringLiteral("SSH_ASKPASS"), askpass);
        env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
        env.insert(QStringLiteral("CERES_ASKPASS"), QStringLiteral("1"));
        env.insert(QStringLiteral("CERES_SSH_PASSWORD"), job.sshPassword);
    }

#ifdef Q_OS_WIN
    // The bundled rsync is a Cygwin/MSYS binary; for an `-e ssh` job it execs
    // "ssh" through PATH. Prepend rsync's own directory so it picks up the
    // matching ssh.exe we ship beside it rather than a foreign (e.g. native
    // Windows OpenSSH) one that wouldn't understand POSIX-form key paths.
    if (ArgvBuilder::usesSsh(job)) {
        const QString binDir = QDir::toNativeSeparators(QFileInfo(m_caps.path).absolutePath());
        env.insert(QStringLiteral("PATH"),
                   binDir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));

        // The bundled Cygwin/MSYS ssh finds ~/.ssh via getpwuid → a nonexistent
        // /home/<user> (the MSYS default; it ignores $HOME), so accept-new can't
        // write known_hosts. ArgvBuilder pins -o UserKnownHostsFile to the real
        // profile's .ssh instead; make sure that directory exists first.
        QDir().mkpath(ArgvBuilder::windowsSshDir());
    }
#endif

    m_process->setProcessEnvironment(env);
    m_process->start();
}

void RsyncProcessEngine::pause()
{
    if (!isRunning())
        return;
#ifdef Q_OS_UNIX
    const qint64 pid = m_process->processId();
    if (pid > 0)
        ::kill(static_cast<pid_t>(-pid), SIGSTOP);  // suspend the rsync+ssh group
#endif
#ifdef Q_OS_WIN
    suspendResumeJobProcesses(static_cast<HANDLE>(m_jobObject), /*suspend=*/true);
#endif
}

void RsyncProcessEngine::resume()
{
    if (!isRunning())
        return;
#ifdef Q_OS_UNIX
    const qint64 pid = m_process->processId();
    if (pid > 0)
        ::kill(static_cast<pid_t>(-pid), SIGCONT);
#endif
#ifdef Q_OS_WIN
    suspendResumeJobProcesses(static_cast<HANDLE>(m_jobObject), /*suspend=*/false);
#endif
}

void RsyncProcessEngine::cancel()
{
    if (!isRunning())
        return;

    m_cancelRequested = true;  // so finished() reports "interrupted" on every platform

#ifdef Q_OS_UNIX
    const qint64 pid = m_process->processId();
    if (pid > 0) {
        // Continue first in case the group is suspended (paused), so SIGTERM is
        // actually delivered rather than left pending.
        ::kill(static_cast<pid_t>(-pid), SIGCONT);
        ::kill(static_cast<pid_t>(-pid), SIGTERM);  // negative pid -> whole group
        return;
    }
#endif

#ifdef Q_OS_WIN
    if (m_jobObject) {
        ::TerminateJobObject(static_cast<HANDLE>(m_jobObject), 1);  // rsync + ssh child
        return;
    }
#endif

    m_process->terminate();
}

bool RsyncProcessEngine::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

#ifdef Q_OS_WIN
RsyncProcessEngine::~RsyncProcessEngine()
{
    if (m_jobObject)
        ::CloseHandle(static_cast<HANDLE>(m_jobObject));  // KILL_ON_JOB_CLOSE reaps stragglers
}
#endif
