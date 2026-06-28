#include <QtTest>

#include <QRegularExpression>
#include <QTemporaryDir>

#include "core/PairedDeviceStore.h"
#include "core/PairingCode.h"

class PairingTest : public QObject {
    Q_OBJECT
private slots:
    void codeIsDeterministicAndOrderIndependent();
    void storeRoundTrips();
};

void PairingTest::codeIsDeterministicAndOrderIndependent()
{
    const QString a = QStringLiteral("machine-aaaa-1111");
    const QString b = QStringLiteral("machine-bbbb-2222");

    const QString code = PairingCode::forDevices(a, b);
    QCOMPARE(code, PairingCode::forDevices(b, a));   // argument order doesn't matter
    QCOMPARE(code, PairingCode::forDevices(a, b));   // stable across calls

    // Format: "DDD DDD" — six digits grouped.
    QVERIFY(code.contains(QRegularExpression(QStringLiteral("^\\d{3} \\d{3}$"))));

    // A different pair yields a (almost surely) different code.
    QVERIFY(PairingCode::forDevices(a, QStringLiteral("machine-cccc-3333")) != code);
}

void PairingTest::storeRoundTrips()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("paired.json"));
    PairedDeviceStore store(path);

    QVERIFY(store.loadAll().isEmpty());

    PairedDevice d;
    d.deviceId = QStringLiteral("dev-1");
    d.name = QStringLiteral("Studio NAS");
    d.sshTarget = QStringLiteral("backup@10.0.0.9");
    d.pairedAtMs = 1719500000000LL;
    QVERIFY(store.upsert(d));
    QVERIFY(store.contains(QStringLiteral("dev-1")));
    QCOMPARE(store.load(QStringLiteral("dev-1")).sshTarget, QStringLiteral("backup@10.0.0.9"));
    QCOMPARE(store.load(QStringLiteral("dev-1")).pairedAtMs, 1719500000000LL);

    // upsert replaces by deviceId (no duplicate).
    d.sshTarget = QStringLiteral("backup@nas.local");
    QVERIFY(store.upsert(d));
    QCOMPARE(store.loadAll().size(), 1);
    QCOMPARE(store.load(QStringLiteral("dev-1")).sshTarget, QStringLiteral("backup@nas.local"));

    QVERIFY(store.remove(QStringLiteral("dev-1")));
    QVERIFY(!store.contains(QStringLiteral("dev-1")));
    QVERIFY(!store.remove(QStringLiteral("dev-1")));  // already gone
}

QTEST_MAIN(PairingTest)
#include "tst_pairing.moc"
