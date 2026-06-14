#include "app/PathCompleter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

namespace {
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
}

QString PathCompleter::completeLocal(const QString &input) const
{
    if (input.isEmpty())
        return input;

    // Bare ~ expands straight to the home folder (QDir::homePath() is
    // /Users/<you> on macOS, /home/<you> on Linux).
    if (input == QLatin1String("~"))
        return QDir::homePath() + QLatin1Char('/');

    // Expand a leading ~/ so Tab reveals the real home path rather than keeping ~.
    QString expanded = input;
    if (input.startsWith(QLatin1String("~/")))
        expanded = QDir::homePath() + input.mid(1);

    const int slash = expanded.lastIndexOf(QLatin1Char('/'));
    const QString dirPath = (slash >= 0) ? expanded.left(slash + 1) : QStringLiteral("./");
    const QString prefix = (slash >= 0) ? expanded.mid(slash + 1) : expanded;

    QDir dir(dirPath);
    if (!dir.exists())
        return input;

    QStringList matches;
    for (const QString &e : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (e.startsWith(prefix))
            matches << e;
    }
    if (matches.isEmpty())
        return input;

    QString result = dirPath + longestCommonPrefix(matches);
    if (matches.size() == 1) {  // a single, fully-known entry: mark directories
        if (QFileInfo(dirPath + matches.first()).isDir() && !result.endsWith(QLatin1Char('/')))
            result += QLatin1Char('/');
    }

    return result;
}

void PathCompleter::completeRemote(const QString &input, const QString &sshKey, int port)
{
    const int colon = input.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return;
    const QString target = input.left(colon);  // user@host
    const QString remotePath = input.mid(colon + 1);

    QStringList args{QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                     QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new"),
                     QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5")};
    if (!sshKey.isEmpty())
        args << QStringLiteral("-i") << sshKey;
    if (port > 0)
        args << QStringLiteral("-p") << QString::number(port);
    args << target;

    // Single-quote the partial path (so spaces are safe and it can't inject), but
    // leave the * outside the quotes so the remote shell still globs it.
    QString quoted = remotePath;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    args << QStringLiteral("ls -dp -- '%1'* 2>/dev/null").arg(quoted);

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(QProcessEnvironment::systemEnvironment());  // ssh-agent
    connect(proc, &QProcess::finished, this, [this, proc, input, target](int, QProcess::ExitStatus) {
        const QStringList lines =
            QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        proc->deleteLater();
        const QString lcp = longestCommonPrefix(lines);
        if (!lcp.isEmpty())
            emit remoteCompleted(input, target + QLatin1Char(':') + lcp);
    });
    connect(proc, &QProcess::errorOccurred, proc, [proc](QProcess::ProcessError) { proc->deleteLater(); });
    proc->start(QStringLiteral("ssh"), args);
}
