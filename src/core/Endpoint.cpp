#include "core/Endpoint.h"

Endpoint EndpointParser::parse(const QString &text)
{
    Endpoint e;
    e.text = text;

    if (text.startsWith(QLatin1String("rsync://")) || text.contains(QLatin1String("::"))) {
        e.kind = EndpointKind::Daemon;
        return e;
    }

    const int slash = text.indexOf(QLatin1Char('/'));
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon >= 0 && (slash < 0 || colon < slash)) {
        const QString left = text.left(colon);
        const QString right = (colon + 1 < text.length()) ? text.mid(colon + 1) : QString();

        // Windows drive paths ("C:\dir", "C:/dir", bare "C:") have a single-letter
        // "host" followed by a path separator (or nothing). Treat them as Local so a
        // browse-picked path like "C:/Users/me" — Qt's folder picker normalises to
        // forward slashes — isn't mistaken for an SSH host named "C". rsync applies
        // the same drive-letter heuristic on Windows. A genuine single-letter SSH
        // host must therefore be qualified, e.g. "user@h:/p" or "h.domain:/p".
        const bool isWindowsDrive = left.size() == 1 && left.at(0).isLetter()
            && (right.isEmpty() || right.startsWith(QLatin1Char('/'))
                || right.startsWith(QLatin1Char('\\')));

        const bool hasUser = left.contains('@');
        const bool hasDot = left.contains('.');
        const bool pathLooksRemote = right.startsWith('/') || right.startsWith('~');
        if (!isWindowsDrive && (hasUser || hasDot || pathLooksRemote)) {
            e.kind = EndpointKind::Ssh;
            e.sshTarget = left;
            e.sshPath = right;
        }
        // Ambiguous "label:rest" (no @, no dot in host part, path part does not start
        // with / or ~) is treated as Local. This protects real local paths/filenames
        // containing ':' before any '/' (e.g. "project:v2/data", "backup:2024-06").
        // Common remote forms "host:/path", "user@host:...", "host:~/..." and hosts
        // with dots continue to work as SSH.
    }

    return e;
}

QString EndpointParser::kindName(const QString &text)
{
    switch (kind(text)) {
    case EndpointKind::Ssh:
        return QStringLiteral("ssh");
    case EndpointKind::Daemon:
        return QStringLiteral("daemon");
    case EndpointKind::Local:
        break;
    }
    return QStringLiteral("local");
}

bool EndpointParser::usesSsh(const SyncJob &job)
{
    return isSsh(job.source) || isSsh(job.destination);
}

bool EndpointParser::usesDaemon(const SyncJob &job)
{
    return isDaemon(job.source) || isDaemon(job.destination);
}
