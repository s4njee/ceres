#pragma once

#include <QString>
#include <QStringList>

/// Describes what the located rsync binary can do. Drives ArgvBuilder so we
/// never pass a flag the binary would reject at runtime.
///
/// Background: macOS Sequoia ships openrsync (reports as 2.6.9-compatible but
/// is a separate implementation) which lacks several GNU rsync features:
///   --info=progress2    (live aggregate progress bar)
///   --outbuf=L          (line-buffered output for real-time streaming)
///   --no-inc-recursive  (stable to-chk denominator for progress)
///   --protect-args      (safe whitespace handling over SSH)
/// Ceres detects openrsync and degrades gracefully — you still get the itemized
/// preview, just no live progress bar. Installing GNU rsync via Homebrew
/// (`brew install rsync`) gives the full experience.
/// @ingroup engine
struct RsyncCapabilities {
    /// How the located runtime expects local filesystem paths. Native = pass
    /// through (macOS/Linux). On Windows, rsync is a Cygwin or MSYS2 build; both
    /// standalone flavors map `C:\` to `/cygdrive/c/` (the prefix compiled into
    /// the runtime). The flavor is still detected for diagnostics/notices.
    enum class PathStyle { Native, Cygwin, Msys };

    QString path;
    QString versionString;  // human-readable, for the UI
    int major = 0;
    int minor = 0;
    bool isOpenRsync = false;
    bool found = false;
    PathStyle pathStyle = PathStyle::Native;

    bool supportsInfoProgress2() const
    {
        return found && !isOpenRsync && (major > 3 || (major == 3 && minor >= 1));
    }
    bool supportsOutbuf() const
    {
        return found && !isOpenRsync && (major > 3 || (major == 3 && minor >= 1));
    }
    bool supportsNoIncRecursive() const
    {
        return found && !isOpenRsync && major >= 3;
    }
    bool supportsProtectArgs() const
    {
        return found && !isOpenRsync && major >= 3;
    }
};

/// @ingroup engine
class BinaryLocator {
public:
    // Prefer a modern GNU rsync (Homebrew) over the system openrsync.
    static RsyncCapabilities locateRsync();
    // Run `<path> --version` and parse it. found=false if not runnable.
    static RsyncCapabilities probe(const QString &path);
    // rsync shipped alongside the app, highest priority and checked first by
    // locateRsync(). Uses the platform executable name (rsync.exe on Windows) and
    // covers both a flat layout (next to the app) and a `rsync/bin/` subdir that
    // keeps the binary with its DLLs. Empty when there is no QCoreApplication yet.
    static QStringList bundledRsyncCandidates();
    // Inspect the binary's directory for cygwin1.dll / msys-2.0.dll to decide how
    // local paths must be rewritten (see RsyncCapabilities::PathStyle). On Windows
    // an unrecognised runtime is assumed Cygwin-style; elsewhere it is Native.
    static RsyncCapabilities::PathStyle detectPathStyle(const QString &binaryPath);
};
