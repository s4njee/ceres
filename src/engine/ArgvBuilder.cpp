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

QString ArgvBuilder::prepareEndpoint(const QString &raw, RsyncCapabilities::PathStyle style)
{
    const QString expanded = expandLocalTilde(raw);
    if (EndpointParser::kind(raw) != EndpointKind::Local)
        return expanded;
    return toRsyncLocalPath(expanded, style);
}

QString ArgvBuilder::toRsyncLocalPath(const QString &path, RsyncCapabilities::PathStyle style)
{
    if (style == RsyncCapabilities::PathStyle::Native || path.isEmpty())
        return path;

    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));  // rsync wants forward slashes

    // A Windows file URL reduced to a path can arrive as "/C:/Users/..." (a leading
    // slash before the drive, from naively stripping "file://"). Drop that slash so
    // the drive mapping below recognises it instead of passing it through as a bogus
    // POSIX path that rsync can't create ("/C:/...").
    if (p.size() >= 3 && p.at(0) == QLatin1Char('/') && p.at(1).isLetter()
        && p.at(2) == QLatin1Char(':'))
        p = p.mid(1);

    // Map a drive path ("X:/rest" or bare "X:") onto the runtime's mount root.
    // Standalone Cygwin- AND MSYS2-based rsync builds both resolve Windows drives
    // under /cygdrive by default: that prefix is compiled into the runtime DLL.
    // MSYS2's shorter "/c" form only applies inside a full install whose
    // etc/fstab remaps the cygdrive prefix to "/", which a bundled rsync.exe does
    // not carry (verified: a fstab-less MSYS2 rsync fails on "/c/..." and resolves
    // "/cygdrive/c/..."). So /cygdrive is the reliable choice for both flavors.
    if (p.size() >= 2 && p.at(0).isLetter() && p.at(1) == QLatin1Char(':')) {
        const QChar drive = p.at(0).toLower();
        QString rest = p.mid(2);  // after "X:"
        if (!rest.isEmpty() && !rest.startsWith(QLatin1Char('/')))
            rest.prepend(QLatin1Char('/'));  // "X:foo" is drive-relative
        return QStringLiteral("/cygdrive/") + drive + rest;
    }
    return p;  // already-POSIX, relative, or UNC (//server/share) — slashes normalised
}

QString ArgvBuilder::windowsSshDir()
{
    const QString home = qEnvironmentVariable("USERPROFILE", QDir::homePath());
    return home + QStringLiteral("/.ssh");
}

QStringList ArgvBuilder::build(const SyncJob &job, const RsyncCapabilities &caps, bool dryRun)
{
    BuildOptions options;
    options.dryRun = dryRun;
    return build(job, caps, options);
}

QStringList ArgvBuilder::build(const SyncJob &job, const RsyncCapabilities &caps,
                               BuildOptions options)
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
    if (job.bwLimitKBps > 0)  // cap transfer rate so a big sync doesn't saturate the link
        args << (QStringLiteral("--bwlimit=") + QString::number(job.bwLimitKBps));
    // Overwrite policy (mutually exclusive in the UI): skip files already present, or
    // only replace when the source is newer. Default (neither) overwrites.
    if (job.ignoreExisting)
        args << QStringLiteral("--ignore-existing");
    if (job.updateOnly)
        args << QStringLiteral("--update");
    // Snapshot backups: hardlink files unchanged since a prior snapshot from that dir,
    // so a new snapshot only stores what actually changed.
    if (!job.linkDest.isEmpty())
        args << (QStringLiteral("--link-dest=") + job.linkDest);

    // Always-on machine-readable output that drives the UI.
    args << QStringLiteral("--itemize-changes") << QStringLiteral("--stats");

    // Keep partially transferred files on interruption so a retried ad-hoc transfer
    // resumes where it left off instead of resending from scratch. Scoped to the
    // per-file transfer path (the browse queue); the preview/sync path doesn't need it.
    if (options.perFileProgress)
        args << QStringLiteral("--partial");

    // Capability-gated: keep the command valid on limited rsyncs (openrsync).
    // Per-file mode swaps the aggregate progress2 for rsync's per-file --progress so
    // the parser can report each file individually (and derive the aggregate itself).
    if (options.perFileProgress && caps.supportsInfoProgress2())
        args << QStringLiteral("--progress");
    else if (caps.supportsInfoProgress2())
        args << QStringLiteral("--info=progress2");
    if (caps.supportsOutbuf())
        args << QStringLiteral("--outbuf=L");  // line-buffer so progress streams live
    if (caps.supportsNoIncRecursive())
        args << QStringLiteral("--no-inc-recursive");  // stable to-chk denominator
    if (usesSsh(job) && caps.supportsProtectArgs())
        args << QStringLiteral("--protect-args");  // remote paths with spaces stay intact

    // SSH transport: force non-interactive options. accept-new trusts first-seen
    // hosts but hard-fails on a CHANGED key (MITM protection) — never
    // StrictHostKeyChecking=no.
    if (usesSsh(job)) {
        // Key mode (no password supplied): BatchMode=yes makes a missing/failed key
        // fail cleanly instead of hanging on ssh's /dev/tty prompt that QProcess
        // can't answer. Password mode: BatchMode would *disable* password auth, so
        // drop it and steer ssh to password/keyboard-interactive — the password is
        // fed via SSH_ASKPASS (see RsyncProcessEngine), never on the argv.
        QStringList ssh{QStringLiteral("ssh")};
        if (job.sshPassword.isEmpty()) {
            if (!options.allowInteractiveSsh)
                ssh << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
        } else {
            ssh << QStringLiteral("-o")
                << QStringLiteral("PreferredAuthentications=password,keyboard-interactive")
                << QStringLiteral("-o") << QStringLiteral("NumberOfPasswordPrompts=1");
        }
        ssh << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=accept-new")
            << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=10")
            // Keepalive: probe every 15s and give up after 3 missed replies (~45s), so a
            // long transfer survives an idle NAT and a dead peer is detected promptly.
            << QStringLiteral("-o") << QStringLiteral("ServerAliveInterval=15")
            << QStringLiteral("-o") << QStringLiteral("ServerAliveCountMax=3");
        if (!job.sshKeyPath.isEmpty())
            ssh << QStringLiteral("-i") << toRsyncLocalPath(job.sshKeyPath, caps.pathStyle);
        if (job.sshPort > 0)
            ssh << QStringLiteral("-p") << QString::number(job.sshPort);
#ifdef Q_OS_WIN
        // The bundled Cygwin/MSYS ssh resolves ~/.ssh via getpwuid → a nonexistent
        // /home/<user>, so accept-new can't create it to write known_hosts. Pin the
        // file to the real profile's .ssh (converted); the launcher mkpaths it.
        ssh << QStringLiteral("-o")
            << (QStringLiteral("UserKnownHostsFile=")
                + toRsyncLocalPath(windowsSshDir() + QStringLiteral("/known_hosts"),
                                   caps.pathStyle));
#endif

        QStringList quoted;
        quoted.reserve(ssh.size());
        for (const QString &part : ssh)
            quoted << shellQuoteIfNeeded(part);
        args << QStringLiteral("-e") << quoted.join(QLatin1Char(' '));
    }

    if (options.dryRun)
        args << QStringLiteral("--dry-run");

    // Filter order is significant in rsync; preserve it.
    for (const QString &pattern : job.excludes)
        args << (QStringLiteral("--exclude=") + pattern);

    args.append(job.extraArgs);

    args << prepareEndpoint(job.source, caps.pathStyle)
         << prepareEndpoint(job.destination, caps.pathStyle);
    return args;
}
