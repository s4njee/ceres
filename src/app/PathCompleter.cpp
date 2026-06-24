#include "app/PathCompleter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <utility>

#include "core/Endpoint.h"
#include "engine/ArgvBuilder.h"

namespace {
struct LocalMatch {
    QString name;
    QString fullPath;
};

// Longest common string prefix of a list (empty list -> empty).
QString longestCommonPrefix(const QStringList &items)
{
    if (items.isEmpty())
        return {};
    QString lcp = items.first();
    for (const QString &s : items) {
        int i = 0;
        while (i < lcp.size() && i < s.size() && lcp.at(i) == s.at(i))
            ++i;
        lcp.truncate(i);
    }
    return lcp;
}

QString expandedLocalPath(const QString &input)
{
    if (input == QLatin1String("~"))
        return QDir::homePath() + QLatin1Char('/');
    if (input.startsWith(QLatin1String("~/")))
        return QDir::homePath() + input.mid(1);
    return input;
}

QString shellSingleQuote(QString s)
{
    s.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + s + QLatin1Char('\'');
}

QStringList sshArgsFor(const Endpoint &endpoint, const QString &sshKey, int port,
                       RsyncCapabilities::PathStyle pathStyle)
{
    QStringList args{QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                     QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new"),
                     QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5")};
    if (!sshKey.isEmpty())
        args << QStringLiteral("-i") << ArgvBuilder::toRsyncLocalPath(sshKey, pathStyle);
    if (port > 0)
        args << QStringLiteral("-p") << QString::number(port);
#ifdef Q_OS_WIN
    // Bundled Cygwin/MSYS ssh resolves ~/.ssh via getpwuid → a nonexistent
    // /home/<user>; pin known_hosts to the real profile (see sshEnvironmentFor,
    // which creates the directory).
    args << QStringLiteral("-o")
         << (QStringLiteral("UserKnownHostsFile=")
             + ArgvBuilder::toRsyncLocalPath(
                   ArgvBuilder::windowsSshDir() + QStringLiteral("/known_hosts"), pathStyle));
#endif
    args << endpoint.sshTarget;
    return args;
}

QProcessEnvironment sshEnvironmentFor(const RsyncCapabilities &caps)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();  // ssh-agent

#ifdef Q_OS_WIN
    // Match RsyncProcessEngine: bundled Cygwin/MSYS ssh is launched via PATH and
    // expects POSIX-form key paths.
    if (caps.found && !caps.path.isEmpty()) {
        const QString binDir = QDir::toNativeSeparators(QFileInfo(caps.path).absolutePath());
        env.insert(QStringLiteral("PATH"),
                   binDir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
    }
    // sshArgsFor pins known_hosts to the real profile's .ssh (the bundled ssh's
    // getpwuid home, /home/<user>, doesn't exist); ensure that directory exists.
    QDir().mkpath(ArgvBuilder::windowsSshDir());
#else
    Q_UNUSED(caps);
#endif

    return env;
}

QList<LocalMatch> localMatchesFor(const QString &input)
{
    QList<LocalMatch> matches;
    if (input.isEmpty())
        return matches;

    const QString expanded = expandedLocalPath(input);
    const int slash = expanded.lastIndexOf(QLatin1Char('/'));
    const QString dirPath = (slash >= 0) ? expanded.left(slash + 1) : QStringLiteral("./");
    const QString prefix = (slash >= 0) ? expanded.mid(slash + 1) : expanded;

    QDir dir(dirPath);
    if (!dir.exists())
        return matches;

    const QFileInfoList entries =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (!name.startsWith(prefix))
            continue;

        QString full = dirPath + name;
        if (entry.isDir() && !full.endsWith(QLatin1Char('/')))
            full += QLatin1Char('/');
        matches << LocalMatch{name, full};
    }
    return matches;
}
}

PathCompleter::PathCompleter(QObject *parent)
    : QObject(parent)
{
}

PathCompleter::PathCompleter(RsyncCapabilities caps, QObject *parent)
    : QObject(parent), m_caps(std::move(caps))
{
}

QString PathCompleter::completeLocal(const QString &input) const
{
    if (input.isEmpty())
        return input;
    if (input == QLatin1String("~"))
        return QDir::homePath() + QLatin1Char('/');

    const QString expanded = expandedLocalPath(input);
    const int slash = expanded.lastIndexOf(QLatin1Char('/'));
    const QString dirPath = (slash >= 0) ? expanded.left(slash + 1) : QStringLiteral("./");

    const QList<LocalMatch> localMatches = localMatchesFor(input);
    QStringList matches;
    for (const LocalMatch &match : localMatches)
        matches << match.name;
    if (matches.isEmpty())
        return input;

    QString result = dirPath + longestCommonPrefix(matches);
    if (localMatches.size() == 1)
        result = localMatches.first().fullPath;

    return result;
}

QStringList PathCompleter::localChoices(const QString &input, int maxChoices) const
{
    const QList<LocalMatch> matches = localMatchesFor(input);
    if (matches.isEmpty() || maxChoices <= 0 || matches.size() > maxChoices)
        return {};

    QStringList choices;
    choices.reserve(matches.size());
    for (const LocalMatch &match : matches)
        choices << match.fullPath;
    return choices;
}

void PathCompleter::completeRemote(const QString &input, const QString &sshKey, int port, int maxChoices)
{
    const Endpoint endpoint = EndpointParser::parse(input);
    if (endpoint.kind != EndpointKind::Ssh || endpoint.sshTarget.isEmpty())
        return;

    QStringList args = sshArgsFor(endpoint, sshKey, port, m_caps.pathStyle);

    // Single-quote the partial path (so spaces are safe and it can't inject), but
    // leave the * outside the quotes so the remote shell still globs it.
    args << QStringLiteral("ls -dp -- %1* 2>/dev/null | head -n %2")
                .arg(shellSingleQuote(endpoint.sshPath), QString::number(qMax(1, maxChoices) + 1));

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(sshEnvironmentFor(m_caps));
    connect(proc, &QProcess::finished, this,
            [this, proc, input, target = endpoint.sshTarget, maxChoices](int, QProcess::ExitStatus) {
        const QStringList lines =
            QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        proc->deleteLater();
        if (lines.size() > maxChoices) {
            emit remoteCompleted(input, input, {});
            return;
        }

        const QString lcp = longestCommonPrefix(lines);
        if (!lcp.isEmpty()) {
            QStringList choices;
            choices.reserve(lines.size());
            for (const QString &line : lines)
                choices << (target + QLatin1Char(':') + line);
            emit remoteCompleted(input, target + QLatin1Char(':') + lcp, choices);
        }
    });
    connect(proc, &QProcess::errorOccurred, proc, [proc](QProcess::ProcessError) { proc->deleteLater(); });
    proc->start(QStringLiteral("ssh"), args);
}

void PathCompleter::browseRemote(const QString &input, const QString &sshKey, int port, int maxEntries)
{
    const Endpoint endpoint = EndpointParser::parse(input);
    if (endpoint.kind != EndpointKind::Ssh || endpoint.sshTarget.isEmpty())
        return;

    const int limit = qMax(1, maxEntries) + 1;
    const QString remoteDir = endpoint.sshPath.isEmpty() ? QStringLiteral(".") : endpoint.sshPath;
    QStringList args = sshArgsFor(endpoint, sshKey, port, m_caps.pathStyle);
    args << QStringLiteral(
                "cd -- %1 2>/dev/null || { printf '__CERES_ERROR__Cannot open folder\\n'; exit 2; }; "
                "pwd=$(pwd -P); case \"$pwd\" in */) ;; *) pwd=\"$pwd/\";; esac; "
                "printf '__CERES_PWD__%s\\n' \"$pwd\"; "
                "find . -mindepth 1 -maxdepth 1 -type d -print 2>/dev/null "
                "| sed 's#^\\./##' | sort | head -n %2 "
                "| while IFS= read -r d; do "
                "case \"$d\" in .|..) continue;; esac; "
                "printf '%s%s/\\n' \"$pwd\" \"$d\"; "
                "done")
                .arg(shellSingleQuote(remoteDir), QString::number(limit));

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(sshEnvironmentFor(m_caps));
    connect(proc, &QProcess::finished, this,
            [this, proc, input, target = endpoint.sshTarget, maxEntries](int, QProcess::ExitStatus) {
        const QStringList lines =
            QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString errorOutput = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const bool failed = proc->exitStatus() != QProcess::NormalExit || proc->exitCode() != 0;
        proc->deleteLater();

        QString current;
        QString error = failed ? QStringLiteral("Could not list remote folder") : QString{};
        QStringList directories;
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("__CERES_ERROR__"))) {
                error = line.mid(QStringLiteral("__CERES_ERROR__").size());
                continue;
            }
            if (line.startsWith(QLatin1String("__CERES_PWD__"))) {
                current = target + QLatin1Char(':') + line.mid(QStringLiteral("__CERES_PWD__").size());
                continue;
            }
            directories << (target + QLatin1Char(':') + line);
        }

        if (directories.size() > maxEntries) {
            directories.clear();
            error = QStringLiteral("Too many folders to show");
        }
        if (current.isEmpty())
            current = input;
        if (!errorOutput.isEmpty() && error.isEmpty())
            error = errorOutput;

        emit remoteBrowseCompleted(input, current, directories, error);
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, input](QProcess::ProcessError) {
        const QString reason = proc->errorString();
        proc->deleteLater();
        emit remoteBrowseCompleted(input, input, {}, reason);
    });
    proc->start(QStringLiteral("ssh"), args);
}
