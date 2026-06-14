#include "engine/BinaryLocator.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

RsyncCapabilities BinaryLocator::probe(const QString &path)
{
    RsyncCapabilities caps;

    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isExecutable())
        return caps;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(path, {QStringLiteral("--version")});
    if (!proc.waitForStarted(2000))
        return caps;
    proc.waitForFinished(3000);

    const QString out = QString::fromUtf8(proc.readAll());

    caps.path = path;
    caps.found = true;
    caps.isOpenRsync = out.contains(QStringLiteral("openrsync"), Qt::CaseInsensitive);

    static const QRegularExpression versionRe(QStringLiteral(R"(version\s+(\d+)\.(\d+))"));
    const auto m = versionRe.match(out);
    if (m.hasMatch()) {
        caps.major = m.captured(1).toInt();
        caps.minor = m.captured(2).toInt();
    }

    caps.versionString = caps.isOpenRsync
        ? QStringLiteral("openrsync (%1.%2-compatible)").arg(caps.major).arg(caps.minor)
        : QStringLiteral("%1.%2").arg(caps.major).arg(caps.minor);
    return caps;
}

RsyncCapabilities BinaryLocator::locateRsync()
{
    QStringList candidates = {
        QStringLiteral("/opt/homebrew/bin/rsync"),  // Apple Silicon Homebrew
        QStringLiteral("/usr/local/bin/rsync"),      // Intel Homebrew / common Linux
    };
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("rsync"));
    if (!onPath.isEmpty())
        candidates << onPath;
    candidates << QStringLiteral("/usr/bin/rsync");  // last resort (openrsync on macOS)

    RsyncCapabilities firstFound;
    for (const QString &cand : candidates) {
        const RsyncCapabilities caps = probe(cand);
        if (!caps.found)
            continue;
        if (!firstFound.found)
            firstFound = caps;
        if (!caps.isOpenRsync)
            return caps;  // a modern GNU rsync wins outright
    }
    return firstFound;  // openrsync, or an all-default (found=false) if nothing ran
}
