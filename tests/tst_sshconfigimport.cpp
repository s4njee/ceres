#include <QtTest>

#include "core/SshConfigImport.h"

class SshConfigImportTest : public QObject {
    Q_OBJECT
private slots:
    void parsesHostsAndFields();
    void skipsWildcardPatterns();
};

void SshConfigImportTest::parsesHostsAndFields()
{
    const QString cfg = QStringLiteral(
        "# my hosts\n"
        "Host web\n"
        "    HostName 10.0.0.5\n"
        "    User deploy\n"
        "    Port 2222\n"
        "    IdentityFile ~/.ssh/web_key\n"
        "\n"
        "Host db\n"
        "    HostName db.internal\n");

    const QList<SshHost> hosts = SshConfigImport::parse(cfg);
    QCOMPARE(hosts.size(), 2);

    QCOMPARE(hosts.at(0).label, QStringLiteral("web"));
    QCOMPARE(hosts.at(0).host, QStringLiteral("10.0.0.5"));
    QCOMPARE(hosts.at(0).user, QStringLiteral("deploy"));
    QCOMPARE(hosts.at(0).target, QStringLiteral("deploy@10.0.0.5"));
    QCOMPARE(hosts.at(0).sshPort, 2222);
    QVERIFY(hosts.at(0).sshKeyPath.endsWith(QStringLiteral("/.ssh/web_key")));
    QVERIFY(!hosts.at(0).sshKeyPath.startsWith(QLatin1Char('~')));  // tilde expanded

    // No User -> target is just the hostname; default port stays 0.
    QCOMPARE(hosts.at(1).label, QStringLiteral("db"));
    QCOMPARE(hosts.at(1).target, QStringLiteral("db.internal"));
    QCOMPARE(hosts.at(1).sshPort, 0);
}

void SshConfigImportTest::skipsWildcardPatterns()
{
    const QString cfg = QStringLiteral(
        "Host *\n"
        "    User root\n"
        "Host *.example.com\n"
        "    User admin\n"
        "Host real\n"
        "    HostName real.example.com\n");

    const QList<SshHost> hosts = SshConfigImport::parse(cfg);
    // Only the concrete "real" host survives; the two pattern blocks are skipped.
    QCOMPARE(hosts.size(), 1);
    QCOMPARE(hosts.at(0).label, QStringLiteral("real"));
    QCOMPARE(hosts.at(0).target, QStringLiteral("real.example.com"));
}

QTEST_MAIN(SshConfigImportTest)
#include "tst_sshconfigimport.moc"
