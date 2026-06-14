#include "engine/ArgvBuilder.h"

#include <QDir>

namespace {
// rsync:// or host::module — the daemon protocol, which does not use ssh.
bool isDaemon(const QString &p)
{
    return p.startsWith(QStringLiteral("rsync://")) || p.contains(QStringLiteral("::"));
}
// user@host:path / host:path — a colon before any slash, and not a daemon spec.
bool isRemoteSsh(const QString &p)
{
    if (isDaemon(p))
        return false;
    const int slash = p.indexOf(QLatin1Char('/'));
    const int colon = p.indexOf(QLatin1Char(':'));
    return colon >= 0 && (slash < 0 || colon < slash);
}

// Expand a leading ~ to the home dir for LOCAL paths only. We invoke rsync via
// QProcess (no shell), so nothing expands ~ for us; remote (ssh/daemon) tildes
// are left for the remote shell to expand.
QString expandLocalTilde(const QString &p)
{
    if (isRemoteSsh(p) || isDaemon(p))
        return p;
    if (p == QLatin1String("~"))
        return QDir::homePath();
    if (p.startsWith(QLatin1String("~/")))
        return QDir::homePath() + p.mid(1);
    return p;
}
} // namespace

bool ArgvBuilder::usesSsh(const SyncJob &job)
{
    return isRemoteSsh(job.source) || isRemoteSsh(job.destination);
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
        args << QStringLiteral("-e") << ssh.join(QLatin1Char(' '));
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
