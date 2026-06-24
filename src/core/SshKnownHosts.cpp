#include "core/SshKnownHosts.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "engine/ArgvBuilder.h"

bool SshKnownHosts::looksLikeChangedHostKey(const QString &stderrText)
{
    return stderrText.contains(QStringLiteral("REMOTE HOST IDENTIFICATION HAS CHANGED"))
        || (stderrText.contains(QStringLiteral("Offending"))
            && stderrText.contains(QStringLiteral("known_hosts")))
        || (stderrText.contains(QStringLiteral("Host key verification failed"))
            && stderrText.contains(QStringLiteral("known_hosts")));
}

QString SshKnownHosts::hostFromTarget(const QString &target)
{
    const int at = target.lastIndexOf(QLatin1Char('@'));
    return at >= 0 ? target.mid(at + 1) : target;
}

KnownHostRepairResult SshKnownHosts::removeHost(const RsyncCapabilities &caps,
                                                const QString &target, int port)
{
    const QString program = sshKeygenProgram(caps);
    if (program.isEmpty()) {
        return {false, QStringLiteral("ssh-keygen was not found; remove the old known_hosts entry manually")};
    }

    const QString file = knownHostsPath();
    if (!QFileInfo::exists(file)) {
        return {false, QStringLiteral("No known_hosts file found at %1").arg(file)};
    }

    QProcess proc;
    proc.start(program, {QStringLiteral("-R"), knownHostsPattern(target, port),
                         QStringLiteral("-f"), file});
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished();
        return {false, QStringLiteral("Timed out while updating known_hosts")};
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return {false, err.isEmpty() ? QStringLiteral("Could not update known_hosts") : err};
    }
    return {true, QStringLiteral("Removed old host key for %1").arg(hostFromTarget(target))};
}

QString SshKnownHosts::knownHostsPath()
{
#ifdef Q_OS_WIN
    return ArgvBuilder::windowsSshDir() + QStringLiteral("/known_hosts");
#else
    return QDir::homePath() + QStringLiteral("/.ssh/known_hosts");
#endif
}

QString SshKnownHosts::sshKeygenProgram(const RsyncCapabilities &caps)
{
#ifdef Q_OS_WIN
    if (caps.found && !caps.path.isEmpty()) {
        const QString exe =
            QFileInfo(caps.path).absoluteDir().filePath(QStringLiteral("ssh-keygen.exe"));
        if (QFileInfo::exists(exe))
            return exe;
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
}

QString SshKnownHosts::knownHostsPattern(const QString &target, int port)
{
    const QString host = hostFromTarget(target);
    if (port > 0 && port != 22)
        return QStringLiteral("[%1]:%2").arg(host, QString::number(port));
    return host;
}
