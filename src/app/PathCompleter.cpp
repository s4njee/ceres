#include "app/PathCompleter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <utility>

#include "app/SshCommand.h"
#include "core/Endpoint.h"
#include "engine/ArgvBuilder.h"

using namespace SshCommand;  // shared ssh argv / env / quoting helpers

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

void PathCompleter::completeRemote(const QString &input, const QString &sshKey, int port,
                                   int maxChoices, const QString &sshPassword)
{
    const Endpoint endpoint = EndpointParser::parse(input);
    if (endpoint.kind != EndpointKind::Ssh || endpoint.sshTarget.isEmpty())
        return;

    QStringList args = sshArgsFor(endpoint, sshKey, port, m_caps.pathStyle, !sshPassword.isEmpty());
    args << QStringLiteral("/bin/sh");  // run our POSIX script via sh, whatever the login shell

    // Single-quote the partial path (so spaces are safe and it can't inject), but
    // leave the * outside the quotes so the remote shell still globs it.
    const QString script = QStringLiteral("ls -dp -- %1* 2>/dev/null | head -n %2\n")
                .arg(shellPathArg(endpoint.sshPath), QString::number(qMax(1, maxChoices) + 1));

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(sshEnvironmentFor(m_caps, sshPassword));
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
    proc->start(sshProgramFor(m_caps), args);
    proc->write(script.toUtf8());  // feed the script to the remote sh over stdin
    proc->closeWriteChannel();
}

void PathCompleter::browseRemote(const QString &input, const QString &sshKey, int port,
                                 int maxEntries, const QString &sshPassword)
{
    const Endpoint endpoint = EndpointParser::parse(input);
    if (endpoint.kind != EndpointKind::Ssh || endpoint.sshTarget.isEmpty())
        return;

    const int limit = qMax(1, maxEntries) + 1;
    const QString remoteDir = endpoint.sshPath.isEmpty() ? QStringLiteral(".") : endpoint.sshPath;
    QStringList args = sshArgsFor(endpoint, sshKey, port, m_caps.pathStyle, !sshPassword.isEmpty());
    args << QStringLiteral("/bin/sh");  // run our POSIX script via sh, whatever the login shell
    const QString script = QStringLiteral(
                "cd -- %1 2>/dev/null || { msg=$(cd -- %1 2>&1); "
                "printf '__CERES_ERROR__%s\\n' \"${msg:-Cannot open folder}\"; exit 2; }; "
                "pwd=$(pwd -P); case \"$pwd\" in */) ;; *) pwd=\"$pwd/\";; esac; "
                "printf '__CERES_PWD__%s\\n' \"$pwd\"; "
                "find . -mindepth 1 -maxdepth 1 -type d -print 2>/dev/null "
                "| sed 's#^\\./##' | sort | head -n %2 "
                "| while IFS= read -r d; do "
                "case \"$d\" in .|..) continue;; esac; "
                "printf '%s%s/\\n' \"$pwd\" \"$d\"; "
                "done\n")
                .arg(shellPathArg(remoteDir), QString::number(limit));

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(sshEnvironmentFor(m_caps, sshPassword));
    connect(proc, &QProcess::finished, this,
            [this, proc, input, target = endpoint.sshTarget, maxEntries]
            (int, QProcess::ExitStatus) {
        const QStringList lines =
            QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString errorOutput = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const bool failed = proc->exitStatus() != QProcess::NormalExit || proc->exitCode() != 0;
        proc->deleteLater();

        // Auth failed (key rejected, or a reused password that's for the wrong user
        // because the target has no "user@") — ask the UI to prompt for credentials
        // and retry. Prompt regardless of whether a password was already tried: the
        // user may need to supply the right username. The modal requires an explicit
        // submit, so this can't loop on its own.
        if (failed && looksLikeAuthFailure(errorOutput)) {
            const int at = target.lastIndexOf(QLatin1Char('@'));
            const QString host = at >= 0 ? target.mid(at + 1) : target;
            const QString user = at >= 0 ? target.left(at) : QString();
            emit remoteAuthRequired(input, host, user);
            return;
        }

        QString current;
        QString error;
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
        // Surface the real remote/ssh stderr when something went wrong, instead of a
        // generic message that hides it. Only fall back to the generic text when the
        // process failed but said nothing useful.
        if (error.isEmpty() && failed)
            error = errorOutput.isEmpty() ? QStringLiteral("Could not list remote folder")
                                          : errorOutput;

        emit remoteBrowseCompleted(input, current, directories, error);
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, input](QProcess::ProcessError) {
        const QString reason = proc->errorString();
        proc->deleteLater();
        emit remoteBrowseCompleted(input, input, {}, reason);
    });
    proc->start(sshProgramFor(m_caps), args);
    proc->write(script.toUtf8());  // feed the script to the remote sh over stdin
    proc->closeWriteChannel();
}
