#include <QtTest>

#include "core/Peer.h"
#include "net/Beacon.h"

class BeaconTest : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void byeFlag();
    void rejectsGarbage();
    void rejectsWrongProtocol();
};

void BeaconTest::roundTrip()
{
    Peer p;
    p.id = QStringLiteral("machine-123");
    p.name = QStringLiteral("studio-linux");
    p.addresses = {QStringLiteral("10.3.60.71"), QStringLiteral("100.81.0.5")};
    p.os = QStringLiteral("Linux");
    p.version = QStringLiteral("0.1");
    p.accepts = {QStringLiteral("ssh")};

    Peer r;
    QVERIFY(Beacon::decode(Beacon::encode(p), r));
    QCOMPARE(r.id, p.id);
    QCOMPARE(r.name, p.name);
    QCOMPARE(r.addresses, p.addresses);
    QCOMPARE(r.os, p.os);
    QCOMPARE(r.accepts, p.accepts);
    QVERIFY(!r.leaving);
    QCOMPARE(r.primaryAddress(), QStringLiteral("10.3.60.71"));
}

void BeaconTest::byeFlag()
{
    Peer p;
    p.id = QStringLiteral("m1");
    p.leaving = true;

    Peer r;
    QVERIFY(Beacon::decode(Beacon::encode(p), r));
    QVERIFY(r.leaving);
}

void BeaconTest::rejectsGarbage()
{
    Peer r;
    QVERIFY(!Beacon::decode(QByteArrayLiteral("not json at all"), r));
    QVERIFY(!Beacon::decode(QByteArrayLiteral("{}"), r));  // no protocol marker
}

void BeaconTest::rejectsWrongProtocol()
{
    Peer r;
    QVERIFY(!Beacon::decode(QByteArrayLiteral("{\"ceres\":99,\"id\":\"x\"}"), r));
    // Right protocol but no id is invalid too.
    QVERIFY(!Beacon::decode(QByteArrayLiteral("{\"ceres\":1}"), r));
}

QTEST_MAIN(BeaconTest)
#include "tst_beacon.moc"
