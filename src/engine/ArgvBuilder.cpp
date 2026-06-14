#include "engine/ArgvBuilder.h"

#include <QDir>

#include "core/Endpoint.h"

namespace {
// Expand a leading ~ to the home dir for LOCAL paths only. We invoke rsync via
// QProcess (no shell), so nothing expands ~ for us; remote (ssh/daemon) tildes
// are left for the remote shell to expand.
QString expandLocalTilde(const QString &p)
{
    if (EndpointParser::kind(p) != EndpointKind::Local)
        return p;
    if (p == QLatin1String("~"))
        return QDir::homePath();
    if (p.startsWith(QLatin1String("~/")))
        return QDir::homePath() + p.mid(1);
    return p;
}

QString shellQuoteIfNeeded(QString s)
{
    if (s.isEmpty())
        return QStringLiteral("''");
    bool safe = true;
    for (const QChar ch : s) {
        const ushort c = ch.unicode();
        const bool alphaNum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9');
        const bool punctuation = ch == QLatin1Char('/') || ch == QLatin1Char('.')
            || ch == QLatin1Char('_') || ch == QLatin1Char('-') || ch == QLatin1Char('=')
            || ch == QLatin1Char(':') || ch == QLatin1Char('@') || ch == QLatin1Char('+')
            || ch == QLatin1Char(',');
        if (!alphaNum && !punctuation) {
            safe = false;
            break;
        }
    }
    if (safe)
        return s;
    s.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QStringLiteral("'") + s + QStringLiteral("'");
}
} // namespace

bool ArgvBuilder::usesSsh(const SyncJob &job)
{
    return EndpointParser::usesSsh(job);
}

QStringList ArgvBuilder::build(const SyncJob &job, const RsyncCapabilities &caps, bool dryRun)
{
    QStringList args;

    if (job.archive)
        args << QStringLiteral("-a");
    if (job.compress)
        args << QStringLiteral("-z");
    if (job.checksum)
        args << QStringLiteral("-c");
    if (job.deleteExtraneous) {
        args << QStringLiteral("--delete");
        if (job.maxDelete > 0)  // abort instead of mass-deleting (e.g. an empty/unmounted source)
            args << (QStringLiteral("--max-delete=") + QString::number(job.maxDelete));
    }

    // Always-on machine-readable output that drives the UI.
    args << QStringLiteral("--itemize-changes") << QStringLiteral("--stats");

    // Capability-gated: keep the command valid on limited rsyncs (openrsync).
    if (caps.supportsInfoProgress2())
        args << QStringLiteral("--info=progress2");
    if (caps.supportsOutbuf())
        args << QStringLiteral("--outbuf=L");  // line-buffer so progress streams live
    if (caps.supportsNoIncRecursive())
        args << QStringLiteral("--no-inc-recursive");  // stable to-chk denominator
    if (usesSsh(job) && caps.supportsProtectArgs())
        args << QStringLiteral("--protect-args");  // remote paths with spaces stay intact

    // SSH transport: force non-interactive options. QProcess can't answer ssh's
    // /dev/tty password/host-key prompts, so BatchMode=yes fails cleanly instead
    // of hanging. accept-new trusts first-seen hosts but hard-fails on a CHANGED
    // key (MITM protection) — never StrictHostKeyChecking=no.
    if (usesSsh(job)) {
        QStringList ssh{QStringLiteral("ssh"),
                        QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new"),
                        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10")};
        if (!job.sshKeyPath.isEmpty())
            ssh << QStringLiteral("-i") << job.sshKeyPath;
        if (job.sshPort > 0)
            ssh << QStringLiteral("-p") << QString::number(job.sshPort);

        QStringList quoted;
        quoted.reserve(ssh.size());
        for (const QString &part : ssh)
            quoted << shellQuoteIfNeeded(part);
        args << QStringLiteral("-e") << quoted.join(QLatin1Char(' '));
    }

    if (dryRun)
        args << QStringLiteral("--dry-run");

    // Filter order is significant in rsync; preserve it.
    for (const QString &pattern : job.excludes)
        args << (QStringLiteral("--exclude=") + pattern);

    args.append(job.extraArgs);

    args << expandLocalTilde(job.source) << expandLocalTilde(job.destination);
    return args;
}
