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

#include "app/SshCommand.h"
#include "core/Endpoint.h"
#include "core/SshKnownHosts.h"

using namespace SshCommand;  // shared ssh argv / env / quoting helpers

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

void RemoteFs::symlink(const QString &target, const QString &dir, const QString &linkName,
                       const QString &pointsTo, const QString &sshKey, int port,
                       const QString &password)
{
    // -s symbolic, -f replace an existing link, -n treat an existing link-to-dir as a
    // file (so we replace it rather than create one inside it).
    const QString script = QStringLiteral("cd -- %1 && ln -sfn -- %2 %3\n")
                .arg(shellPathArg(dir), shellSingleQuote(pointsTo), shellSingleQuote(linkName));
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
