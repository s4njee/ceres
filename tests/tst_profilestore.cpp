#include <QtTest>

#include <QTemporaryDir>

#include "core/ProfileStore.h"
#include "core/SyncJob.h"

class ProfileStoreTest : public QObject {
    Q_OBJECT
private slots:
    void jsonRoundTrip();
    void saveLoadDelete();
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

QTEST_MAIN(ProfileStoreTest)
#include "tst_profilestore.moc"
