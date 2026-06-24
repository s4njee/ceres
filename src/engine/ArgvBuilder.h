#pragma once

#include <QStringList>

#include "core/SyncJob.h"
#include "engine/BinaryLocator.h"  // RsyncCapabilities

/// Translates a SyncJob into an rsync argument vector. Pure and capability-aware:
/// progress2 / outbuf / no-inc-recursive are only emitted when the located rsync
/// actually supports them (macOS' system openrsync does not). Kept free of side
/// effects so it is exhaustively unit-testable.
/// @ingroup engine
class ArgvBuilder {
public:
    static QStringList build(const SyncJob &job, const RsyncCapabilities &caps, bool dryRun);

    /// True if either endpoint is a remote SSH spec (`user@host:path` / `host:path`),
    /// as opposed to local or an rsync:// daemon target.
    static bool usesSsh(const SyncJob &job);

    /// Rewrite a LOCAL path into the form the located rsync runtime expects. Native
    /// runtimes pass through unchanged; Cygwin/MSYS2 runtimes (Windows) get drive
    /// paths mapped to POSIX under the /cygdrive prefix, e.g. `C:\Users\me` →
    /// `/cygdrive/c/Users/me`. Remote specs must not be passed here.
    static QString toRsyncLocalPath(const QString &path, RsyncCapabilities::PathStyle style);

    /// Windows: the user's ssh directory (`%USERPROFILE%\.ssh`) in native form.
    /// The bundled Cygwin/MSYS `ssh.exe` locates `~/.ssh` via `getpwuid()` — which
    /// yields a nonexistent `/home/<user>` (the MSYS compiled-in default), not
    /// `$HOME` — so `build()` pins it to this directory via `-o UserKnownHostsFile`
    /// and the launcher must `mkpath` it first. Reads `USERPROFILE`; no side effects.
    static QString windowsSshDir();
};
