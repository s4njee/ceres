#include "engine/RsyncProcessEngine.h"

#include <QProcessEnvironment>
#include <QStringList>
#include <utility>

#include "engine/ArgvBuilder.h"

#ifdef Q_OS_UNIX
#  include <csignal>
#  include <sys/types.h>
#  include <unistd.h>
#endif

RsyncProcessEngine::RsyncProcessEngine(RsyncCapabilities caps, QObject *parent)
    : SyncEngine(parent), m_caps(std::move(caps))
{
    // Forward parser output straight through as engine signals.
    connect(&m_parser, &OutputParser::change, this, &SyncEngine::change);
    connect(&m_parser, &OutputParser::progress, this, &SyncEngine::progress);
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
                emit finished(exitCode, status == QProcess::CrashExit);
            });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit failedToStart(m_process->errorString());
    });

#ifdef Q_OS_UNIX
    // Become a process-group leader so cancel() can signal the whole group.
    m_process->setChildProcessModifier([] { ::setpgid(0, 0); });
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

    ensureProcess();
    m_parser.reset();

    m_process->setProgram(m_caps.path);
    m_process->setArguments(ArgvBuilder::build(job, m_caps, dryRun));
    m_process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());  // carries SSH_AUTH_SOCK

    emit started();
    m_process->start();
}

void RsyncProcessEngine::cancel()
{
    if (!isRunning())
        return;

#ifdef Q_OS_UNIX
    const qint64 pid = m_process->processId();
    if (pid > 0) {
        ::kill(static_cast<pid_t>(-pid), SIGTERM);  // negative pid -> whole group
        return;
    }
#endif
    m_process->terminate();
}

bool RsyncProcessEngine::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}
