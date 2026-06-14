#include <QtTest>

#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/ProfileStore.h"
#include "core/SyncJob.h"

class ProfileStoreTest : public QObject {
    Q_OBJECT
private slots:
    void jsonRoundTrip();
    void saveLoadDelete();
    void scheduleRoundTrip();
    void presentJobIdsSeesUnparseableFiles();
};

void ProfileStoreTest::jsonRoundTrip()
{
    SyncJob j;
    j.id = QStringLiteral("abc123");
    j.name = QStringLiteral("Docs → NAS");
    j.source = QStringLiteral("~/Documents/");
    j.destination = QStringLiteral("user@nas:/backup/");
    j.archive = true;
    j.compress = true;
    j.deleteExtraneous = true;
    j.checksum = false;
    j.excludes = {QStringLiteral("*.tmp"), QStringLiteral("node_modules/")};

    const SyncJob r = ProfileStore::fromJson(ProfileStore::toJson(j));

    QCOMPARE(r.id, j.id);
    QCOMPARE(r.name, j.name);
    QCOMPARE(r.source, j.source);
    QCOMPARE(r.destination, j.destination);
    QVERIFY(r.archive);
    QVERIFY(r.compress);
    QVERIFY(r.deleteExtraneous);
    QVERIFY(!r.checksum);
    QCOMPARE(r.excludes, j.excludes);
}

void ProfileStoreTest::saveLoadDelete()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const ProfileStore store(tmp.path());

    SyncJob a;
    a.id = QStringLiteral("id-a");
    a.name = QStringLiteral("Job A");
    a.source = QStringLiteral("a/");
    a.destination = QStringLiteral("A/");
    SyncJob b;
    b.id = QStringLiteral("id-b");
    b.name = QStringLiteral("Job B");
    b.source = QStringLiteral("b/");
    b.destination = QStringLiteral("B/");

    QVERIFY(store.save(a));
    QVERIFY(store.save(b));

    QList<SyncJob> all = store.loadAll();
    QCOMPARE(all.size(), 2);

    // A job with no id must not be persisted.
    SyncJob noId;
    noId.name = QStringLiteral("nope");
    QVERIFY(!store.save(noId));

    QVERIFY(store.remove(QStringLiteral("id-a")));
    all = store.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().id, QStringLiteral("id-b"));
}

void ProfileStoreTest::scheduleRoundTrip()
{
    SyncJob j;
    j.id = QStringLiteral("sched-1");
    j.name = QStringLiteral("Nightly");
    j.schedule = ScheduleKind::Weekly;
    j.weekday = 3;
    j.atHour = 14;
    j.atMinute = 30;
    j.intervalMinutes = 45;

    const SyncJob r = ProfileStore::fromJson(ProfileStore::toJson(j));
    QCOMPARE(r.schedule, ScheduleKind::Weekly);
    QCOMPARE(r.weekday, 3);
    QCOMPARE(r.atHour, 14);
    QCOMPARE(r.atMinute, 30);
    QCOMPARE(r.intervalMinutes, 45);

    // Default (no schedule key) decodes as Manual.
    QCOMPARE(ProfileStore::fromJson(QJsonObject{}).schedule, ScheduleKind::Manual);
}

void ProfileStoreTest::presentJobIdsSeesUnparseableFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const ProfileStore store(tmp.path());

    SyncJob good;
    good.id = QStringLiteral("good-1");
    good.name = QStringLiteral("Good");
    QVERIFY(store.save(good));

    // A corrupt profile that loadAll() will skip, but whose file is on disk.
    QFile bad(tmp.path() + QStringLiteral("/corrupt-2.json"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{ this is not valid json");
    bad.close();

    // loadAll drops the corrupt one...
    QCOMPARE(store.loadAll().size(), 1);
    // ...but presentJobIds reports BOTH, so the prune can't mistake a transiently
    // unreadable profile for a deleted job.
    const QStringList present = store.presentJobIds();
    QCOMPARE(present.size(), 2);
    QVERIFY(present.contains(QStringLiteral("good-1")));
    QVERIFY(present.contains(QStringLiteral("corrupt-2")));
}

QTEST_MAIN(ProfileStoreTest)
#include "tst_profilestore.moc"
