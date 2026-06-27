#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include "core/Endpoint.h"
#include "engine/BinaryLocator.h"

/// Shared ssh-invocation plumbing for the browse / path-completion features
/// (RemoteFs, PathCompleter): how the `ssh` argv is built (key-vs-password auth,
/// host-key policy, keepalive, known_hosts pinning on Windows), how the ssh binary
/// is located, and how a password is fed non-interactively via the SSH_ASKPASS hook.
///
/// This was previously copy-pasted as file-local statics in each translation unit;
/// the copies drifted (keepalive options landed in only one), which is exactly why
/// it now lives in one place.
/// @ingroup app
namespace SshCommand {

/// Single-quote a string for a POSIX shell (doubling embedded quotes).
QString shellSingleQuote(QString s);

/// Quote a remote path for the shell, but leave a leading "~"/"~user" prefix
/// unquoted so the remote shell still expands it (matching rsync's tilde handling).
QString shellPathArg(const QString &path);

/// Build the `ssh` option argv for a target. Key mode uses BatchMode; password mode
/// steers ssh to a single SSH_ASKPASS-fed prompt. Always pins host-key policy,
/// connect timeout, and keepalive; pins known_hosts on Windows.
QStringList sshArgsFor(const Endpoint &endpoint, const QString &sshKey, int port,
                       RsyncCapabilities::PathStyle pathStyle, bool passwordMode);

/// The ssh program to run (the bundled ssh.exe beside rsync on Windows, else "ssh").
QString sshProgramFor(const RsyncCapabilities &caps);

/// Process environment for an ssh launch: inherits ssh-agent, wires SSH_ASKPASS when
/// a password is supplied, and (Windows) puts the bundled bin on PATH + ensures the
/// known_hosts directory exists.
QProcessEnvironment sshEnvironmentFor(const RsyncCapabilities &caps, const QString &sshPassword);

/// True if ssh stderr indicates an authentication failure (vs an unreachable host).
bool looksLikeAuthFailure(const QString &stderrText);

/// An Endpoint carrying `target` as its ssh target (all the ssh plumbing reads).
Endpoint endpointForTarget(const QString &target);

/// Split "user@host" (or bare "host") on the last '@'; a bare host yields empty user.
void splitTarget(const QString &target, QString &host, QString &user);

}  // namespace SshCommand
