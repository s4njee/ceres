#include <QtTest>

#include <QDateTime>

#include "core/Snapshot.h"

class SnapshotTest : public QObject {
    Q_OBJECT
private slots:
    void nameIsSortableAndRecognized();
    void latestPicksNewestIgnoringJunk();
};

void SnapshotTest::nameIsSortableAndRecognized()
{
    const QDateTime t(QDate(2026, 6, 27), QTime(14, 3, 12));
    const QString name = Snapshot::nameForTime(t);
    QCOMPARE(name, QStringLiteral("2026-06-27-140312"));
    QVERIFY(Snapshot::isSnapshotName(name));

    // Non-snapshot entries (the latest symlink, stray dirs) are rejected.
    QVERIFY(!Snapshot::isSnapshotName(QStringLiteral("latest")));
    QVERIFY(!Snapshot::isSnapshotName(QStringLiteral("2026-06-27")));
    QVERIFY(!Snapshot::isSnapshotName(QStringLiteral("backup-1")));

    // Lexical order matches chronological order (the whole point of the format).
    QVERIFY(Snapshot::nameForTime(QDateTime(QDate(2026, 1, 1), QTime(0, 0, 0)))
            < Snapshot::nameForTime(QDateTime(QDate(2026, 1, 1), QTime(0, 0, 1))));
}

void SnapshotTest::latestPicksNewestIgnoringJunk()
{
    const QStringList listing = {QStringLiteral("2026-06-20-100000"), QStringLiteral("latest"),
                                 QStringLiteral("2026-06-27-140312"), QStringLiteral("notes.txt"),
                                 QStringLiteral("2026-06-25-090000")};

    QCOMPARE(Snapshot::latestOf(listing), QStringLiteral("2026-06-27-140312"));

    const QStringList sorted = Snapshot::sortedSnapshots(listing);  // newest first, junk dropped
    QCOMPARE(sorted.size(), 3);
    QCOMPARE(sorted.first(), QStringLiteral("2026-06-27-140312"));
    QCOMPARE(sorted.last(), QStringLiteral("2026-06-20-100000"));

    QVERIFY(Snapshot::latestOf({QStringLiteral("latest")}).isEmpty());  // no snapshots yet
}

QTEST_MAIN(SnapshotTest)
#include "tst_snapshot.moc"
