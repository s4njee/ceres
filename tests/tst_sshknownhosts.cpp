#include <QtTest>

#include "core/SshKnownHosts.h"

class SshKnownHostsTest : public QObject {
    Q_OBJECT
private slots:
    void detectsChangedHostKey();
    void extractsHostFromTarget();
};

void SshKnownHostsTest::detectsChangedHostKey()
{
    QVERIFY(SshKnownHosts::looksLikeChangedHostKey(
        QStringLiteral("@ WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED! @\n"
                       "Offending ED25519 key in /home/me/.ssh/known_hosts:3")));
    QVERIFY(SshKnownHosts::looksLikeChangedHostKey(
        QStringLiteral("Host key verification failed.\n"
                       "Offending key for IP in /home/me/.ssh/known_hosts:4")));
    QVERIFY(!SshKnownHosts::looksLikeChangedHostKey(
        QStringLiteral("Permission denied (publickey,password).")));
}

void SshKnownHostsTest::extractsHostFromTarget()
{
    QCOMPARE(SshKnownHosts::hostFromTarget(QStringLiteral("pi@raspberrypi.local")),
             QStringLiteral("raspberrypi.local"));
    QCOMPARE(SshKnownHosts::hostFromTarget(QStringLiteral("192.168.1.50")),
             QStringLiteral("192.168.1.50"));
}

QTEST_MAIN(SshKnownHostsTest)
#include "tst_sshknownhosts.moc"
