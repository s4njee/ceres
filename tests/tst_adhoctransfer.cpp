#include <QtTest>

#include <QDir>

#include "cli/AdHocTransfer.h"

class AdHocTransferTest : public QObject {
    Q_OBJECT
private:
    RsyncCapabilities modern() const
    {
        RsyncCapabilities c;
        c.found = true;
        c.path = QStringLiteral("/usr/bin/rsync");
        c.major = 3;
        c.minor = 2;
        c.isOpenRsync = false;
        return c;
    }

private slots:
    void argumentsMatchCliTransferDefaults();
    void rendersProgressBar();
};

void AdHocTransferTest::argumentsMatchCliTransferDefaults()
{
    const QStringList args = AdHocTransfer::buildArguments(
        QStringLiteral("user@host:/folder"), QStringLiteral("~/destfolder"), modern());

    QVERIFY(args.contains(QStringLiteral("-axh")));
    QVERIFY(args.contains(QStringLiteral("--info=progress2")));
    QVERIFY(args.contains(QStringLiteral("--outbuf=L")));
    QVERIFY(!args.contains(QStringLiteral("-a")));
    QCOMPARE(args.last(), QDir::homePath() + QStringLiteral("/destfolder"));

    const int ei = args.indexOf(QStringLiteral("-e"));
    QVERIFY(ei >= 0);
    const QString ssh = args.at(ei + 1);
    QVERIFY(!ssh.contains(QStringLiteral("BatchMode=yes")));
    QVERIFY(ssh.contains(QStringLiteral("StrictHostKeyChecking=accept-new")));
}

void AdHocTransferTest::rendersProgressBar()
{
    ProgressInfo progress;
    progress.percent = 50;
    progress.rate = QStringLiteral("10.00MB/s");
    progress.eta = QStringLiteral("0:00:07");
    progress.toCheck = 2;
    progress.totalToCheck = 10;

    const QString plain = AdHocTransfer::renderProgressLine(progress, 80, false);
    QVERIFY(plain.contains(QLatin1Char('#')));
    QVERIFY(plain.contains(QLatin1Char('-')));
    QVERIFY(plain.contains(QStringLiteral(" 50%")));
    QVERIFY(plain.contains(QStringLiteral("files 8/10")));

    const QString colored = AdHocTransfer::renderProgressLine(progress, 80, true);
    QVERIFY(colored.contains(QStringLiteral("\x1b[32m")));
}

QTEST_MAIN(AdHocTransferTest)
#include "tst_adhoctransfer.moc"
