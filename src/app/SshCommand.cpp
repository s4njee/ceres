#include "app/SshCommand.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "engine/ArgvBuilder.h"

namespace SshCommand {

QString shellSingleQuote(QString s)
{
    s.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + s + QLatin1Char('\'');
}

QString shellPathArg(const QString &path)
{
    // Leave a leading "~"/"~user" prefix UNQUOTED so the remote shell still expands it
    // to the home directory (single-quoting the whole path defeats expansion and makes
    // `cd` fail). Everything after the tilde segment is single-quoted against injection.
    if (!path.startsWith(QLatin1Char('~')))
        return shellSingleQuote(path);
    const int slash = path.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return path;  // "~" or "~user" alone — usernames are shell-safe
    return path.left(slash) + QLatin1Char('/') + shellSingleQuote(path.mid(slash + 1));
}

QStringList sshArgsFor(const Endpoint &endpoint, const QString &sshKey, int port,
                       RsyncCapabilities::PathStyle pathStyle, bool passwordMode)
{
    // Mirror ArgvBuilder: key mode uses BatchMode (fail fast instead of hanging on a
    // prompt QProcess can't answer); password mode drops it and steers ssh to a
    // single password prompt fed through SSH_ASKPASS (see sshEnvironmentFor).
    QStringList args;
    if (passwordMode) {
        args << QStringLiteral("-o")
             << QStringLiteral("PreferredAuthentications=password,keyboard-interactive")
             << QStringLiteral("-o") << QStringLiteral("NumberOfPasswordPrompts=1");
    } else {
        args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    }
    args << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=accept-new")
         << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=5")
         // Keepalive so a session held open across an idle NAT isn't silently dropped,
         // and a dead peer is detected (~45s) instead of hanging.
         << QStringLiteral("-o") << QStringLiteral("ServerAliveInterval=15")
         << QStringLiteral("-o") << QStringLiteral("ServerAliveCountMax=3");
    if (!sshKey.isEmpty())
        args << QStringLiteral("-i") << ArgvBuilder::toRsyncLocalPath(sshKey, pathStyle);
    if (port > 0)
        args << QStringLiteral("-p") << QString::number(port);
#ifdef Q_OS_WIN
    // Bundled Cygwin/MSYS ssh resolves ~/.ssh via getpwuid → a nonexistent
    // /home/<user>; pin known_hosts to the real profile (see sshEnvironmentFor,
    // which creates the directory).
    args << QStringLiteral("-o")
         << (QStringLiteral("UserKnownHostsFile=")
             + ArgvBuilder::toRsyncLocalPath(
                   ArgvBuilder::windowsSshDir() + QStringLiteral("/known_hosts"), pathStyle));
#endif
    args << endpoint.sshTarget;
    return args;
}

QString sshProgramFor(const RsyncCapabilities &caps)
{
#ifdef Q_OS_WIN
    if (caps.found && !caps.path.isEmpty()) {
        const QString bundledSsh =
            QFileInfo(caps.path).absoluteDir().filePath(QStringLiteral("ssh.exe"));
        if (QFileInfo::exists(bundledSsh))
            return bundledSsh;
    }
#else
    Q_UNUSED(caps);
#endif
    return QStringLiteral("ssh");
}

QProcessEnvironment sshEnvironmentFor(const RsyncCapabilities &caps, const QString &sshPassword)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();  // ssh-agent

    // Feed a password to ssh non-interactively via the askpass hook (same mechanism as
    // RsyncProcessEngine): this executable, run with CERES_ASKPASS set, prints the
    // password. Never placed on the argv.
    if (!sshPassword.isEmpty()) {
        QString askpass = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
        askpass = ArgvBuilder::toRsyncLocalPath(askpass, caps.pathStyle);
#endif
        env.insert(QStringLiteral("SSH_ASKPASS"), askpass);
        env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
        env.insert(QStringLiteral("CERES_ASKPASS"), QStringLiteral("1"));
        env.insert(QStringLiteral("CERES_SSH_PASSWORD"), sshPassword);
    }

#ifdef Q_OS_WIN
    // Match RsyncProcessEngine: bundled Cygwin/MSYS ssh is launched via PATH and
    // expects POSIX-form key paths.
    if (caps.found && !caps.path.isEmpty()) {
        const QString binDir = QDir::toNativeSeparators(QFileInfo(caps.path).absolutePath());
        env.insert(QStringLiteral("PATH"),
                   binDir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
    }
    // sshArgsFor pins known_hosts to the real profile's .ssh (the bundled ssh's
    // getpwuid home, /home/<user>, doesn't exist); ensure that directory exists.
    QDir().mkpath(ArgvBuilder::windowsSshDir());
#else
    Q_UNUSED(caps);
#endif

    return env;
}

bool looksLikeAuthFailure(const QString &stderrText)
{
    return stderrText.contains(QStringLiteral("Permission denied"))
        || stderrText.contains(QStringLiteral("Authentication failed"));
}

Endpoint endpointForTarget(const QString &target)
{
    Endpoint endpoint;
    endpoint.kind = EndpointKind::Ssh;
    endpoint.sshTarget = target;
    return endpoint;
}

void splitTarget(const QString &target, QString &host, QString &user)
{
    const int at = target.lastIndexOf(QLatin1Char('@'));
    host = at >= 0 ? target.mid(at + 1) : target;
    user = at >= 0 ? target.left(at) : QString();
}

}  // namespace SshCommand
