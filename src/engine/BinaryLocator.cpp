#include "engine/BinaryLocator.h"

#include <QCoreApplication>
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
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        proc.waitForFinished(1000);
        return caps;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return caps;

    const QString out = QString::fromUtf8(proc.readAll());
    caps.isOpenRsync = out.contains(QStringLiteral("openrsync"), Qt::CaseInsensitive);

    static const QRegularExpression versionRe(QStringLiteral(R"(version\s+(\d+)\.(\d+))"));
    const auto m = versionRe.match(out);
    if (!m.hasMatch())
        return {};

    caps.path = path;
    caps.found = true;
    caps.pathStyle = detectPathStyle(path);
    caps.major = m.captured(1).toInt();
    caps.minor = m.captured(2).toInt();
    caps.versionString = caps.isOpenRsync
        ? QStringLiteral("openrsync (%1.%2-compatible)").arg(caps.major).arg(caps.minor)
        : QStringLiteral("%1.%2").arg(caps.major).arg(caps.minor);
    return caps;
}

RsyncCapabilities::PathStyle BinaryLocator::detectPathStyle(const QString &binaryPath)
{
    const QString dir = QFileInfo(binaryPath).absolutePath();
    if (QFileInfo::exists(dir + QStringLiteral("/msys-2.0.dll")))
        return RsyncCapabilities::PathStyle::Msys;
    if (QFileInfo::exists(dir + QStringLiteral("/cygwin1.dll")))
        return RsyncCapabilities::PathStyle::Cygwin;
#ifdef Q_OS_WIN
    return RsyncCapabilities::PathStyle::Cygwin;  // unknown Windows runtime — assume cygwin-style
#else
    return RsyncCapabilities::PathStyle::Native;  // a plain Unix rsync
#endif
}

QStringList BinaryLocator::bundledRsyncCandidates()
{
    const QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty())
        return {};  // no QCoreApplication yet — nothing app-relative to offer
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("rsync.exe");
#else
    const QString exe = QStringLiteral("rsync");
#endif
    return {
        dir + QLatin1Char('/') + exe,               // flat: rsync next to the app
        dir + QStringLiteral("/rsync/bin/") + exe,  // subdir keeping rsync with its DLLs
    };
}

RsyncCapabilities BinaryLocator::locateRsync()
{
    // A bundled rsync shipped beside the app wins — it's the build we tested against
    // (and on Windows, the only one we can rely on existing).
    QStringList candidates = bundledRsyncCandidates();

#ifndef Q_OS_WIN
    candidates << QStringLiteral("/opt/homebrew/bin/rsync")  // Apple Silicon Homebrew
               << QStringLiteral("/usr/local/bin/rsync");    // Intel Homebrew / common Linux
#endif
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("rsync"));
    if (!onPath.isEmpty())
        candidates << onPath;
#ifndef Q_OS_WIN
    candidates << QStringLiteral("/usr/bin/rsync");  // last resort (openrsync on macOS)
#endif

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
