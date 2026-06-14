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
        e.kind = EndpointKind::Ssh;
        e.sshTarget = text.left(colon);
        e.sshPath = text.mid(colon + 1);
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
