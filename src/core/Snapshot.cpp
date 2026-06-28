#include "core/Snapshot.h"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>
#include <functional>

namespace Snapshot {

QString nameForTime(const QDateTime &when)
{
    // Local time, lexically sortable, filesystem-safe (no ':' for Windows dests).
    return when.toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
}

bool isSnapshotName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}-\\d{6}$"));
    return re.match(name).hasMatch();
}

QStringList sortedSnapshots(const QStringList &dirNames)
{
    QStringList snaps;
    for (const QString &n : dirNames) {
        if (isSnapshotName(n))
            snaps << n;
    }
    std::sort(snaps.begin(), snaps.end(), std::greater<QString>());  // newest first
    return snaps;
}

QString latestOf(const QStringList &dirNames)
{
    const QStringList sorted = sortedSnapshots(dirNames);
    return sorted.isEmpty() ? QString() : sorted.first();
}

}  // namespace Snapshot
