#pragma once

#include <QString>
#include <QStringList>

class QDateTime;

/// Naming and selection logic for `--link-dest` snapshot backups.
///
/// A snapshot destination is a base directory holding timestamped subdirectories,
/// one per backup run: `<base>/2026-06-27-140312/`. Each new snapshot rsyncs with
/// `--link-dest=../<previous>` so files unchanged since the last run are hardlinked
/// rather than re-copied — N snapshots cost ~one copy of the data plus deltas. A
/// `latest` symlink in the base points at the newest snapshot.
///
/// The timestamp format is lexically sortable, so "newest" is just the string max.
/// Pure and unit-tested; the actual remote directory work lives elsewhere.
/// @ingroup core
namespace Snapshot {

/// The conventional `latest` symlink name in a snapshot base directory.
inline QString latestLinkName() { return QStringLiteral("latest"); }

/// Snapshot directory name for a moment in time, e.g. "2026-06-27-140312".
QString nameForTime(const QDateTime &when);

/// True if `name` is a snapshot directory name we'd create (YYYY-MM-DD-HHMMSS).
bool isSnapshotName(const QString &name);

/// Snapshot names from a directory listing, sorted newest-first.
QStringList sortedSnapshots(const QStringList &dirNames);

/// The newest snapshot name in a listing, or empty if there are none.
QString latestOf(const QStringList &dirNames);

}  // namespace Snapshot
