#include <QtTest>

#include "core/Endpoint.h"

class EndpointTest : public QObject {
    Q_OBJECT
private slots:
    void classifiesLocalSshAndDaemon();
    void jobHelpersSeeTransport();
};

void EndpointTest::classifiesLocalSshAndDaemon()
{
    QCOMPARE(EndpointParser::kindName(QStringLiteral("/tmp/a:b")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("user@host:/backup")), QStringLiteral("ssh"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("host:/backup")), QStringLiteral("ssh"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("rsync://host/module")), QStringLiteral("daemon"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("host::module")), QStringLiteral("daemon"));

    const Endpoint ssh = EndpointParser::parse(QStringLiteral("user@host:~/backup"));
    QVERIFY(ssh.kind == EndpointKind::Ssh);
    QCOMPARE(ssh.sshTarget, QStringLiteral("user@host"));
    QCOMPARE(ssh.sshPath, QStringLiteral("~/backup"));
}

void EndpointTest::jobHelpersSeeTransport()
{
    SyncJob j;
    j.source = QStringLiteral("/tmp/source");
    j.destination = QStringLiteral("host:/backup");
    QVERIFY(EndpointParser::usesSsh(j));
    QVERIFY(!EndpointParser::usesDaemon(j));

    j.destination = QStringLiteral("rsync://host/module");
    QVERIFY(!EndpointParser::usesSsh(j));
    QVERIFY(EndpointParser::usesDaemon(j));
}

QTEST_MAIN(EndpointTest)
#include "tst_endpoint.moc"
