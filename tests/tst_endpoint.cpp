#include <QtTest>

#include "core/Endpoint.h"

class EndpointTest : public QObject {
    Q_OBJECT
private slots:
    void classifiesLocalSshAndDaemon();
    void classifiesWindowsDrivePaths();
    void jobHelpersSeeTransport();
};

void EndpointTest::classifiesLocalSshAndDaemon()
{
    // Positives (must continue working)
    QCOMPARE(EndpointParser::kindName(QStringLiteral("/tmp/a:b")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("user@host:/backup")), QStringLiteral("ssh"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("host:/backup")), QStringLiteral("ssh"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("host.with.dot:/path")), QStringLiteral("ssh"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("rsync://host/module")), QStringLiteral("daemon"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("host::module")), QStringLiteral("daemon"));

    const Endpoint ssh = EndpointParser::parse(QStringLiteral("user@host:~/backup"));
    QVERIFY(ssh.kind == EndpointKind::Ssh);
    QCOMPARE(ssh.sshTarget, QStringLiteral("user@host"));
    QCOMPARE(ssh.sshPath, QStringLiteral("~/backup"));

    // Negatives: local paths/names containing ':' before any '/' must NOT be treated
    // as SSH (these were previously misclassified by the old colon-before-slash rule).
    QCOMPARE(EndpointParser::kindName(QStringLiteral("project:v2/data")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("backup:2024-06-14")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("my:weird:filename")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("foo:bar/baz")), QStringLiteral("local"));
}

void EndpointTest::classifiesWindowsDrivePaths()
{
    // Browse-picked Windows paths come through with forward slashes (Qt normalises
    // to '/'); both slash styles and a bare drive must be Local, not an SSH host
    // named after the drive letter.
    QCOMPARE(EndpointParser::kindName(QStringLiteral("C:/Users/me")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("C:\\Users\\me")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("D:/data/backup")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("c:/lower")), QStringLiteral("local"));
    QCOMPARE(EndpointParser::kindName(QStringLiteral("C:")), QStringLiteral("local"));

    // The drive letter must not leak into the SSH fields.
    const Endpoint drive = EndpointParser::parse(QStringLiteral("C:/Users/me"));
    QVERIFY(drive.kind == EndpointKind::Local);
    QVERIFY(drive.sshTarget.isEmpty());
    QVERIFY(drive.sshPath.isEmpty());

    // Escape hatch: a genuine single-letter SSH host still works when qualified.
    QCOMPARE(EndpointParser::kindName(QStringLiteral("user@c:/path")), QStringLiteral("ssh"));
    // Multi-character hosts are unaffected by the drive heuristic.
    QCOMPARE(EndpointParser::kindName(QStringLiteral("host:/backup")), QStringLiteral("ssh"));
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
