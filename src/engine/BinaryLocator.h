#pragma once

#include <QString>

// What the located rsync can do. Drives ArgvBuilder so we never pass a flag the
// binary rejects. macOS Sequoia ships openrsync (2.6.9-compatible), which lacks
// --info=progress2 / --outbuf / --no-inc-recursive — so we detect and adapt.
struct RsyncCapabilities {
    QString path;
    QString versionString;  // human-readable, for the UI
    int major = 0;
    int minor = 0;
    bool isOpenRsync = false;
    bool found = false;

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
};

class BinaryLocator {
public:
    // Prefer a modern GNU rsync (Homebrew) over the system openrsync.
    static RsyncCapabilities locateRsync();
    // Run `<path> --version` and parse it. found=false if not runnable.
    static RsyncCapabilities probe(const QString &path);
};
