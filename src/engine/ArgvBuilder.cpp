#include "engine/ArgvBuilder.h"

QStringList ArgvBuilder::build(const SyncJob &job, const RsyncCapabilities &caps, bool dryRun)
{
    QStringList args;

    if (job.archive)
        args << QStringLiteral("-a");
    if (job.compress)
        args << QStringLiteral("-z");
    if (job.checksum)
        args << QStringLiteral("-c");
    if (job.deleteExtraneous)
        args << QStringLiteral("--delete");

    // Always-on machine-readable output that drives the UI.
    args << QStringLiteral("--itemize-changes") << QStringLiteral("--stats");

    // Capability-gated: keep the command valid on limited rsyncs (openrsync).
    if (caps.supportsInfoProgress2())
        args << QStringLiteral("--info=progress2");
    if (caps.supportsOutbuf())
        args << QStringLiteral("--outbuf=L");  // line-buffer so progress streams live
    if (caps.supportsNoIncRecursive())
        args << QStringLiteral("--no-inc-recursive");  // stable to-chk denominator

    if (dryRun)
        args << QStringLiteral("--dry-run");

    // Filter order is significant in rsync; preserve it.
    for (const QString &pattern : job.excludes)
        args << (QStringLiteral("--exclude=") + pattern);

    args.append(job.extraArgs);

    args << job.source << job.destination;
    return args;
}
