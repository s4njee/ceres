#pragma once

#include <QString>

/// One entry in a directory listing — local (via QDir) or remote (parsed from
/// `ls -lA` by RemoteFs). Plain data so it's trivially copyable and testable.
/// `mtimeText` is kept as the listing's own date string rather than a parsed
/// timestamp: remote `ls` formats are not portably machine-readable, and the
/// browser only needs to display it.
/// @ingroup core
struct FileEntry {
    QString name;            // base name (no path), e.g. "report.pdf"
    bool isDir = false;      // directory (or a symlink resolving to one, best-effort)
    bool isSymlink = false;  // listing showed a symlink (mode 'l')
    qint64 size = 0;         // bytes (0 for directories we don't size)
    QString mtimeText;       // human date as shown by the source, e.g. "Jun 22 10:30"
};
