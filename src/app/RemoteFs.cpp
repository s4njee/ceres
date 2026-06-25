#include "app/RemoteFs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>
#include <functional>
#include <utility>

#include "core/Endpoint.h"
#include "core/SshKnownHosts.h"
#include "engine/ArgvBuilder.h"

namespace {
// These ssh helpers are deliberate copies of PathCompleter.cpp's file-local
// statics: they are private to that translation unit, so duplicating them here
// keeps PathCompleter untouched while RemoteFs shares the exact same ssh
// behaviour (key vs password auth, host-key policy, known_hosts pinning).

QString shellSingleQuote(QString s)
{
    s.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + s + QLatin1Char('\'');
}

// Quote a remote path for the shell, but leave a leading "~" / "~user" prefix
// UNQUOTED so the remote shell still expands it to the home directory. rsync expands
// tildes on the remote too, so a browse/complete must match — single-quoting the
// whole path (e.g. '~/Backups') defeats expansion and makes `cd` fail. Everything
// after the tilde segment is single-quoted against injection.
QString shellPathArg(const QString &path)
{
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
         // Keepalive so a browse session held open across an idle NAT isn't silently
         // dropped, and a dead peer is detected (~45s) instead of hanging.
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

// True if ssh stderr indicates the connection failed authentication (vs unreachable).
bool looksLikeAuthFailure(const QString &stderrText)
{
    return stderrText.contains(QStringLiteral("Permission denied"))
        || stderrText.contains(QStringLiteral("Authentication failed"));
}

// Build an Endpoint that carries `target` as its ssh target. sshArgsFor only reads
// endpoint.sshTarget, so this is all the ssh plumbing needs.
Endpoint endpointForTarget(const QString &target)
{
    Endpoint endpoint;
    endpoint.kind = EndpointKind::Ssh;
    endpoint.sshTarget = target;
    return endpoint;
}

// Split "user@host" (or "host") on the last '@' so the UI can prefill the password
// prompt. A bare host yields an empty user.
void splitTarget(const QString &target, QString &host, QString &user)
{
    const int at = target.lastIndexOf(QLatin1Char('@'));
    host = at >= 0 ? target.mid(at + 1) : target;
    user = at >= 0 ? target.left(at) : QString();
}
}  // namespace

RemoteFs::RemoteFs(RsyncCapabilities caps, QObject *parent)
    : QObject(parent), m_caps(std::move(caps))
{
}

void RemoteFs::list(const QString &target, const QString &dir, const QString &sshKey, int port,
                    const QString &password)
{
    const Endpoint endpoint = endpointForTarget(target);
    QStringList args = sshArgsFor(endpoint, sshKey, port, m_caps.pathStyle, !password.isEmpty());
    args << QStringLiteral("/bin/sh");  // run our POSIX script via sh, whatever the login shell

    // cd into the dir (capturing the shell's own error message on failure), echo the
    // resolved absolute path, then a plain `ls -lA` block parsed by parseLsList. LC_ALL=C
    // pins the date/column format we parse.
    const QString script = QStringLiteral(
                "cd -- %1 2>/dev/null || { msg=$(cd -- %1 2>&1); "
                "printf '__CERES_ERROR__%s\\n' \"${msg:-Cannot open folder}\"; exit 2; }; "
                "pwd=$(pwd -P); case \"$pwd\" in */) ;; *) pwd=\"$pwd/\";; esac; "
                "printf '__CERES_PWD__%s\\n' \"$pwd\"; "
                "LC_ALL=C ls -lA 2>/dev/null\n")
                .arg(shellPathArg(dir));

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(sshEnvironmentFor(m_caps, password));
    connect(proc, &QProcess::finished, this,
            [this, proc, target, dir](int, QProcess::ExitStatus) {
        const QStringList lines =
            QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'));
        const QString errorOutput = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const bool failed = proc->exitStatus() != QProcess::NormalExit || proc->exitCode() != 0;
        proc->deleteLater();

        if (failed && SshKnownHosts::looksLikeChangedHostKey(errorOutput)) {
            emit hostKeyChanged(target, SshKnownHosts::hostFromTarget(target));
            return;
        }

        // Auth failed (key rejected, or a reused password that's for the wrong user) —
        // ask the UI to prompt for credentials and retry.
        if (failed && looksLikeAuthFailure(errorOutput)) {
            QString host;
            QString user;
            splitTarget(target, host, user);
            emit authRequired(target, host, user);
            return;
        }

        QString resolved;
        QString error;
        QString lsBlock;
        bool seenPwd = false;
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("__CERES_ERROR__"))) {
                error = line.mid(QStringLiteral("__CERES_ERROR__").size());
                continue;
            }
            if (line.startsWith(QLatin1String("__CERES_PWD__"))) {
                resolved = line.mid(QStringLiteral("__CERES_PWD__").size());
                seenPwd = true;
                continue;
            }
            // Everything after the pwd marker is the `ls -lA` body.
            if (seenPwd)
                lsBlock += line + QLatin1Char('\n');
        }

        if (resolved.isEmpty())
            resolved = dir;
        // Surface the real remote/ssh stderr when something went wrong, rather than a
        // generic message that hides it.
        if (error.isEmpty() && failed)
            error = errorOutput.isEmpty() ? QStringLiteral("Could not list folder") : errorOutput;

        const QList<FileEntry> entries = error.isEmpty() ? parseLsList(lsBlock) : QList<FileEntry>{};
        emit listed(target, resolved, entries, error);
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, target, dir](QProcess::ProcessError) {
        const QString reason = proc->errorString();
        proc->deleteLater();
        emit listed(target, dir, {}, reason);
    });
    proc->start(sshProgramFor(m_caps), args);
    proc->write(script.toUtf8());  // feed the script to the remote sh over stdin
    proc->closeWriteChannel();
}

namespace {
// Run a script over ssh and hand its trimmed stdout (or an error string) to `handler`.
// Like runOpCommand but it captures stdout — used by the read-only scalar queries
// (du/df) that need the command's output, not just its exit status.
void runQueryCommand(RemoteFs *self, const RsyncCapabilities &caps, const QString &target,
                     const QString &sshKey, int port, const QString &password,
                     const QString &script,
                     std::function<void(const QString &out, const QString &error)> handler)
{
    const Endpoint endpoint = endpointForTarget(target);
    QStringList args = sshArgsFor(endpoint, sshKey, port, caps.pathStyle, !password.isEmpty());
    args << QStringLiteral("/bin/sh");

    auto *proc = new QProcess(self);
    proc->setProcessEnvironment(sshEnvironmentFor(caps, password));
    QObject::connect(proc, &QProcess::finished, self,
                     [proc, handler = std::move(handler)](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        const QString errOut = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const bool failed = proc->exitStatus() != QProcess::NormalExit || proc->exitCode() != 0;
        proc->deleteLater();
        handler(out, failed ? (errOut.isEmpty() ? QStringLiteral("Command failed") : errOut)
                            : QString());
    });
    QObject::connect(proc, &QProcess::errorOccurred, self,
                     [proc, handler](QProcess::ProcessError) {
        const QString reason = proc->errorString();
        proc->deleteLater();
        handler(QString(), reason);
    });
    proc->start(sshProgramFor(caps), args);
    proc->write(script.toUtf8());
    proc->closeWriteChannel();
}
}  // namespace

void RemoteFs::diskUsage(const QString &target, const QString &dir, const QString &name,
                         const QString &sshKey, int port, const QString &password)
{
    // `du -sk` prints "<kbytes>\t<path>"; we take the leading KB count.
    const QString script = QStringLiteral("cd -- %1 2>/dev/null && du -sk -- %2\n")
                .arg(shellPathArg(dir), shellPathArg(name));
    runQueryCommand(this, m_caps, target, sshKey, port, password, script,
                    [this, name](const QString &out, const QString &error) {
        if (!error.isEmpty()) {
            emit diskUsageReady(name, 0, error);
            return;
        }
        bool ok = false;
        const qint64 kb = out.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toLongLong(&ok);
        emit diskUsageReady(name, ok ? kb * 1024 : 0,
                            ok ? QString() : QStringLiteral("Could not read folder size"));
    });
}

void RemoteFs::freeSpace(const QString &target, const QString &dir, const QString &sshKey, int port,
                         const QString &password)
{
    // `df -Pk .` (POSIX format) keeps one line per filesystem: Filesystem, 1024-blocks,
    // Used, Available, Capacity, Mounted-on. We read total (col 2) and available (col 4).
    const QString script = QStringLiteral("cd -- %1 2>/dev/null && df -Pk .\n").arg(shellPathArg(dir));
    runQueryCommand(this, m_caps, target, sshKey, port, password, script,
                    [this](const QString &out, const QString &error) {
        if (!error.isEmpty()) {
            emit freeSpaceReady(0, 0, error);
            return;
        }
        const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("Filesystem")))
                continue;  // header
            const QStringList cols = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                Qt::SkipEmptyParts);
            if (cols.size() >= 4) {
                emit freeSpaceReady(cols.at(3).toLongLong() * 1024, cols.at(1).toLongLong() * 1024,
                                    QString());
                return;
            }
        }
        emit freeSpaceReady(0, 0, QStringLiteral("Could not read free space"));
    });
}

namespace {
// mkdir/remove/rename share the exact same ssh+stdin+auth machinery and differ only
// in the script and that they report via opFinished. This runs `ssh ... /bin/sh`,
// writes `script`, and on completion emits opFinished(error) or authRequired.
void runOpCommand(RemoteFs *self, const RsyncCapabilities &caps, const QString &target,
                  const QString &sshKey, int port, const QString &password, const QString &script)
{
    const Endpoint endpoint = endpointForTarget(target);
    QStringList args = sshArgsFor(endpoint, sshKey, port, caps.pathStyle, !password.isEmpty());
    args << QStringLiteral("/bin/sh");

    auto *proc = new QProcess(self);
    proc->setProcessEnvironment(sshEnvironmentFor(caps, password));
    QObject::connect(proc, &QProcess::finished, self,
                     [self, proc, target](int, QProcess::ExitStatus) {
        const QString errorOutput = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const bool failed = proc->exitStatus() != QProcess::NormalExit || proc->exitCode() != 0;
        proc->deleteLater();

        if (failed && SshKnownHosts::looksLikeChangedHostKey(errorOutput)) {
            emit self->hostKeyChanged(target, SshKnownHosts::hostFromTarget(target));
            return;
        }

        if (failed && looksLikeAuthFailure(errorOutput)) {
            QString host;
            QString user;
            splitTarget(target, host, user);
            emit self->authRequired(target, host, user);
            return;
        }

        QString error;
        if (failed)
            error = errorOutput.isEmpty() ? QStringLiteral("Operation failed") : errorOutput;
        emit self->opFinished(error);
    });
    QObject::connect(proc, &QProcess::errorOccurred, self,
                     [self, proc](QProcess::ProcessError) {
        const QString reason = proc->errorString();
        proc->deleteLater();
        emit self->opFinished(reason);
    });
    proc->start(sshProgramFor(caps), args);
    proc->write(script.toUtf8());
    proc->closeWriteChannel();
}
}  // namespace

void RemoteFs::mkdir(const QString &target, const QString &dir, const QString &name,
                     const QString &sshKey, int port, const QString &password)
{
    // cd into the dir first so the new folder lands relative to the (tilde-expanded)
    // target; the name is single-quoted against injection and to keep spaces literal.
    const QString script = QStringLiteral("cd -- %1 && mkdir -- %2\n")
                .arg(shellPathArg(dir), shellSingleQuote(name));
    runOpCommand(this, m_caps, target, sshKey, port, password, script);
}

void RemoteFs::remove(const QString &target, const QString &dir, const QStringList &names,
                      const QString &sshKey, int port, const QString &password)
{
    QStringList quoted;
    quoted.reserve(names.size());
    for (const QString &name : names)
        quoted << shellSingleQuote(name);
    const QString script = QStringLiteral("cd -- %1 && rm -rf -- %2\n")
                .arg(shellPathArg(dir), quoted.join(QLatin1Char(' ')));
    runOpCommand(this, m_caps, target, sshKey, port, password, script);
}

void RemoteFs::rename(const QString &target, const QString &dir, const QString &from,
                      const QString &to, const QString &sshKey, int port, const QString &password)
{
    const QString script = QStringLiteral("cd -- %1 && mv -- %2 %3\n")
                .arg(shellPathArg(dir), shellSingleQuote(from), shellSingleQuote(to));
    runOpCommand(this, m_caps, target, sshKey, port, password, script);
}

QString RemoteFs::friendlyError(const QString &raw)
{
    struct Rule {
        const char *needle;
        const char *hint;
    };
    // First matching rule wins; ordered most-specific first.
    static const Rule rules[] = {
        {"Host key verification failed", "the host key didn't match — review known hosts"},
        {"Permission denied", "check your username, key, or password"},
        {"Authentication failed", "check your username, key, or password"},
        {"Connection refused", "is the SSH server running on that port?"},
        {"Connection timed out", "the host didn't respond — check the address and firewall"},
        {"Operation timed out", "the host didn't respond — check the address and firewall"},
        {"No route to host", "the host is unreachable — check the address and network"},
        {"Network is unreachable", "check your network connection"},
        {"Could not resolve", "couldn't resolve the hostname"},
        {"Name or service not known", "couldn't resolve the hostname"},
        {"nodename nor servname", "couldn't resolve the hostname"},
        {"No such file or directory", "that path doesn't exist on the remote"},
    };
    for (const Rule &r : rules) {
        const QString hint = QLatin1String(r.hint);
        if (raw.contains(QLatin1String(r.needle), Qt::CaseInsensitive)) {
            // Don't double-append if the raw text already reads like the hint.
            if (raw.contains(hint, Qt::CaseInsensitive))
                return raw;
            return raw.trimmed() + QStringLiteral(" — ") + hint;
        }
    }
    return raw;
}

QList<FileEntry> RemoteFs::parseLsList(const QString &lsOutput)
{
    QList<FileEntry> entries;
    const QStringList lines = lsOutput.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("total ")))
            continue;  // blank lines and the `ls` byte-total header carry no entry

        // Tokenise on runs of whitespace for the fixed leading columns (mode, links,
        // owner, group, size, month, day, time/year). The name is recovered separately
        // below so embedded spaces survive.
        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (tokens.size() < 9)
            continue;  // not a long-format entry (e.g. a stray status line)

        FileEntry entry;
        const QString mode = tokens.at(0);
        entry.isDir = mode.startsWith(QLatin1Char('d'));
        entry.isSymlink = mode.startsWith(QLatin1Char('l'));
        entry.size = tokens.at(4).toLongLong();  // best-effort: 0 if non-numeric
        entry.mtimeText = tokens.at(5) + QLatin1Char(' ') + tokens.at(6) + QLatin1Char(' ')
                + tokens.at(7);

        // The name is everything after the 8th whitespace-delimited field. Rejoining the
        // tokens would collapse runs of spaces inside a name, so instead walk the line
        // past 8 (field + whitespace) groups and take the rest verbatim.
        int pos = 0;
        const int len = line.size();
        for (int field = 0; field < 8; ++field) {
            while (pos < len && !line.at(pos).isSpace())  // skip the field
                ++pos;
            while (pos < len && line.at(pos).isSpace())   // skip the gap to the next field
                ++pos;
        }
        QString name = line.mid(pos);

        // Symlinks list as "link -> target"; split the link's own name from the target.
        const int arrow = name.indexOf(QStringLiteral(" -> "));
        if (entry.isSymlink && arrow >= 0) {
            entry.linkTarget = name.mid(arrow + 4);
            name = name.left(arrow);
        }

        if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
            continue;
        entry.name = name;
        entries << entry;
    }
    return entries;
}
