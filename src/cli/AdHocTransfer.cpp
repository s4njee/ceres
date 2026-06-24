#include "cli/AdHocTransfer.h"

#include <QDir>
#include <QByteArray>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTextStream>

#include <algorithm>
#include <cstdio>

#include "engine/ArgvBuilder.h"
#include "engine/OutputParser.h"

#ifdef Q_OS_UNIX
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

#ifdef Q_OS_WIN
#  include <io.h>
#  include <windows.h>
#endif

namespace {

bool isTerminal(FILE *stream)
{
#ifdef Q_OS_WIN
    return _isatty(_fileno(stream)) != 0;
#else
    return ::isatty(fileno(stream)) != 0;
#endif
}

bool supportsAnsi(FILE *stream)
{
    if (!isTerminal(stream))
        return false;
    if (qEnvironmentVariableIsSet("NO_COLOR"))
        return false;
    if (qgetenv("TERM") == QByteArrayLiteral("dumb"))
        return false;

#ifdef Q_OS_WIN
    const intptr_t osHandle = _get_osfhandle(_fileno(stream));
    if (osHandle == -1)
        return false;
    const HANDLE handle = reinterpret_cast<HANDLE>(osHandle);
    DWORD mode = 0;
    if (!::GetConsoleMode(handle, &mode))
        return false;
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0)
        ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    return true;
}

int terminalColumns()
{
#ifdef Q_OS_UNIX
    winsize size{};
    if (::ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
        return size.ws_col;
#endif
#ifdef Q_OS_WIN
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const intptr_t osHandle = _get_osfhandle(_fileno(stdout));
    if (osHandle != -1
        && ::GetConsoleScreenBufferInfo(reinterpret_cast<HANDLE>(osHandle), &info)) {
        return info.srWindow.Right - info.srWindow.Left + 1;
    }
#endif
    return 80;
}

QString color(const QString &text, const char *code, bool enabled)
{
    if (!enabled)
        return text;
    return QString::fromLatin1("\x1b[") + QString::fromLatin1(code)
        + QLatin1Char('m') + text + QStringLiteral("\x1b[0m");
}

QString eraseLine(bool ansi)
{
    if (ansi)
        return QStringLiteral("\r\x1b[2K");
    return QLatin1Char('\r') + QString(120, QLatin1Char(' ')) + QLatin1Char('\r');
}

class TerminalView {
public:
    TerminalView()
        : m_out(stdout),
          m_err(stderr),
          m_interactive(isTerminal(stdout)),
          m_color(supportsAnsi(stdout))
    {
    }

    void printHeader(const RsyncCapabilities &caps, const QString &source,
                     const QString &destination)
    {
        m_out << "ceres: rsync " << caps.versionString << "  " << caps.path << '\n';
        m_out << "ceres: " << source << " -> " << destination << '\n';
        if (!caps.supportsInfoProgress2()) {
            m_out << "ceres: this rsync does not support aggregate progress; "
                  << "install GNU rsync for the live total bar\n";
        }
        m_out.flush();
    }

    void showStatus(const QString &text)
    {
        if (!m_interactive)
            return;
        m_out << eraseLine(m_color) << color(QStringLiteral("ceres:"), "36", m_color)
              << QLatin1Char(' ') << text;
        m_out.flush();
        m_hasLiveLine = true;
    }

    void showProgress(const ProgressInfo &progress, int changes)
    {
        if (!m_interactive)
            return;

        QString line = AdHocTransfer::renderProgressLine(progress, terminalColumns(), m_color);
        if (changes > 0)
            line += QStringLiteral("  changes ") + QString::number(changes);

        m_out << eraseLine(m_color) << line;
        m_out.flush();
        m_hasLiveLine = true;
    }

    void printEvent(const QString &line)
    {
        if (line.isEmpty())
            return;
        finishLiveLine();
        m_out << line << '\n';
        m_out.flush();
    }

    void printError(const QString &line)
    {
        finishLiveLine();
        m_err << line << '\n';
        m_err.flush();
    }

    void finishLiveLine()
    {
        if (!m_hasLiveLine)
            return;
        m_out << '\n';
        m_out.flush();
        m_hasLiveLine = false;
    }

private:
    QTextStream m_out;
    QTextStream m_err;
    bool m_interactive = false;
    bool m_color = false;
    bool m_hasLiveLine = false;
};

QProcessEnvironment transferEnvironment(const SyncJob &job, const RsyncCapabilities &caps)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

#ifdef Q_OS_WIN
    if (ArgvBuilder::usesSsh(job)) {
        const QString binDir = QDir::toNativeSeparators(QFileInfo(caps.path).absolutePath());
        env.insert(QStringLiteral("PATH"),
                   binDir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
        QDir().mkpath(ArgvBuilder::windowsSshDir());
    }
#else
    Q_UNUSED(job);
    Q_UNUSED(caps);
#endif

    return env;
}

} // namespace

SyncJob AdHocTransfer::makeJob(const QString &source, const QString &destination)
{
    SyncJob job;
    job.name = QStringLiteral("CLI transfer");
    job.source = source;
    job.destination = destination;
    job.archive = false;
    job.extraArgs = {QStringLiteral("-axh")};
    return job;
}

QStringList AdHocTransfer::buildArguments(const QString &source, const QString &destination,
                                          const RsyncCapabilities &caps)
{
    const SyncJob job = makeJob(source, destination);
    ArgvBuilder::BuildOptions options;
    options.allowInteractiveSsh = true;
    return ArgvBuilder::build(job, caps, options);
}

QString AdHocTransfer::renderProgressLine(const ProgressInfo &progress, int columns, bool colorize)
{
    const int percent = qBound(0, progress.percent, 100);
    const QString rate = progress.rate.isEmpty() ? QStringLiteral("--") : progress.rate;
    const QString eta = progress.eta.isEmpty() ? QStringLiteral("--:--:--") : progress.eta;
    QString suffix = QStringLiteral(" %1%  %2  ETA %3")
                         .arg(percent, 3)
                         .arg(rate, eta);

    if (progress.totalToCheck > 0 && progress.toCheck >= 0) {
        const int done = std::max(0, progress.totalToCheck - progress.toCheck);
        suffix += QStringLiteral("  files %1/%2")
                      .arg(done)
                      .arg(progress.totalToCheck);
    }

    const int barWidth = qBound(10, columns - suffix.size() - 4, 44);
    const int filled = qBound(0, (barWidth * percent) / 100, barWidth);
    const QString fill = QString(filled, QLatin1Char('#'));
    const QString rest = QString(barWidth - filled, QLatin1Char('-'));

    const QString bar = color(fill, "32", colorize) + color(rest, "2", colorize);
    return QLatin1Char('[') + bar + QLatin1Char(']') + suffix;
}

int AdHocTransfer::run(const QString &source, const QString &destination)
{
    TerminalView view;
    const RsyncCapabilities caps = BinaryLocator::locateRsync();
    if (!caps.found) {
        view.printError(QStringLiteral("ceres: no rsync binary found"));
        return 69;
    }

    const SyncJob job = makeJob(source, destination);
    const QStringList args = buildArguments(source, destination, caps);
    view.printHeader(caps, source, destination);
    view.showStatus(QStringLiteral("Scanning file list..."));

    QProcess process;
    OutputParser parser;
    QEventLoop loop;
    QStringList stats;
    int changes = 0;
    int result = 70;
    bool sawProgress = false;
    bool done = false;

    process.setProgram(caps.path);
    process.setArguments(args);
    process.setProcessEnvironment(transferEnvironment(job, caps));
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);

    QObject::connect(&process, &QProcess::readyReadStandardOutput, [&] {
        parser.feed(process.readAllStandardOutput());
    });

    QObject::connect(&parser, &OutputParser::change, [&](const ChangeItem &) {
        ++changes;
        if (!sawProgress)
            view.showStatus(QStringLiteral("Scanning file list... %1 change(s)").arg(changes));
    });
    QObject::connect(&parser, &OutputParser::progress, [&](const ProgressInfo &progress) {
        sawProgress = true;
        view.showProgress(progress, changes);
    });
    QObject::connect(&parser, &OutputParser::stats, [&](const QString &line) {
        stats << line;
    });
    QObject::connect(&parser, &OutputParser::log, [&](const QString &line) {
        view.printEvent(line);
    });

    QObject::connect(&process, &QProcess::errorOccurred, [&](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        view.printError(QStringLiteral("ceres: failed to start rsync: %1")
                            .arg(process.errorString()));
        result = 70;
        done = true;
        loop.quit();
    });

    QObject::connect(&process, &QProcess::finished,
                     [&](int exitCode, QProcess::ExitStatus status) {
        parser.feed(process.readAllStandardOutput());
        parser.flush();
        view.finishLiveLine();

        for (const QString &line : stats)
            view.printEvent(line);

        const bool crashed = status == QProcess::CrashExit;
        if (crashed) {
            view.printError(QStringLiteral("ceres: rsync crashed or was interrupted"));
            result = 70;
        } else if (exitCode == 0) {
            view.printEvent(QStringLiteral("ceres: transfer complete (%1 change(s))")
                                .arg(changes));
            result = 0;
        } else {
            view.printError(QStringLiteral("ceres: rsync exited with code %1").arg(exitCode));
            result = exitCode;
        }
        done = true;
        loop.quit();
    });

    process.start();
    if (!done)
        loop.exec();
    return result;
}
