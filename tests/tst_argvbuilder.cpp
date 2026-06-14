#include <QtTest>

#include "core/SyncJob.h"
#include "engine/ArgvBuilder.h"
#include "engine/BinaryLocator.h"

class ArgvBuilderTest : public QObject {
    Q_OBJECT
private:
    RsyncCapabilities modern() const
    {
        RsyncCapabilities c;
        c.found = true;
        c.major = 3;
        c.minor = 2;
        c.isOpenRsync = false;
        return c;
    }
    RsyncCapabilities openrsync() const
    {
        RsyncCapabilities c;
        c.found = true;
        c.major = 2;
        c.minor = 6;
        c.isOpenRsync = true;
        return c;
    }

private slots:
    void modernRsyncGetsFullFlagSet();
    void openRsyncOmitsUnsupportedFlags();
    void excludesPreserveOrder();
    void deleteAndDryRun();
    void sourceAndDestComeLast();
};

void ArgvBuilderTest::modernRsyncGetsFullFlagSet()
{
    SyncJob job;
    job.source = QStringLiteral("a/");
    job.destination = QStringLiteral("b/");
    job.archive = true;

    const QStringList args = ArgvBuilder::build(job, modern(), /*dryRun=*/false);

    QVERIFY(args.contains(QStringLiteral("-a")));
    QVERIFY(args.contains(QStringLiteral("--itemize-changes")));
    QVERIFY(args.contains(QStringLiteral("--stats")));
    QVERIFY(args.contains(QStringLiteral("--info=progress2")));
    QVERIFY(args.contains(QStringLiteral("--outbuf=L")));
    QVERIFY(args.contains(QStringLiteral("--no-inc-recursive")));
    QVERIFY(!args.contains(QStringLiteral("--dry-run")));
}

void ArgvBuilderTest::openRsyncOmitsUnsupportedFlags()
{
    SyncJob job;
    job.source = QStringLiteral("a/");
    job.destination = QStringLiteral("b/");

    const QStringList args = ArgvBuilder::build(job, openrsync(), /*dryRun=*/false);

    // Still get the portable basics...
    QVERIFY(args.contains(QStringLiteral("--itemize-changes")));
    QVERIFY(args.contains(QStringLiteral("--stats")));
    // ...but none of the flags openrsync would reject.
    QVERIFY(!args.contains(QStringLiteral("--info=progress2")));
    QVERIFY(!args.contains(QStringLiteral("--outbuf=L")));
    QVERIFY(!args.contains(QStringLiteral("--no-inc-recursive")));
}

void ArgvBuilderTest::excludesPreserveOrder()
{
    SyncJob job;
    job.source = QStringLiteral("a/");
    job.destination = QStringLiteral("b/");
    job.excludes = {QStringLiteral("*.tmp"), QStringLiteral("node_modules/"),
                    QStringLiteral(".git/")};

    const QStringList args = ArgvBuilder::build(job, modern(), /*dryRun=*/false);

    const int i1 = args.indexOf(QStringLiteral("--exclude=*.tmp"));
    const int i2 = args.indexOf(QStringLiteral("--exclude=node_modules/"));
    const int i3 = args.indexOf(QStringLiteral("--exclude=.git/"));
    QVERIFY(i1 >= 0);
    QVERIFY(i2 > i1);
    QVERIFY(i3 > i2);
}

void ArgvBuilderTest::deleteAndDryRun()
{
    SyncJob job;
    job.source = QStringLiteral("a/");
    job.destination = QStringLiteral("b/");
    job.deleteExtraneous = true;

    const QStringList args = ArgvBuilder::build(job, modern(), /*dryRun=*/true);

    QVERIFY(args.contains(QStringLiteral("--delete")));
    QVERIFY(args.contains(QStringLiteral("--dry-run")));
}

void ArgvBuilderTest::sourceAndDestComeLast()
{
    SyncJob job;
    job.source = QStringLiteral("a/");
    job.destination = QStringLiteral("b/");

    const QStringList args = ArgvBuilder::build(job, modern(), /*dryRun=*/false);

    QCOMPARE(args.at(args.size() - 2), QStringLiteral("a/"));
    QCOMPARE(args.last(), QStringLiteral("b/"));
}

QTEST_MAIN(ArgvBuilderTest)
#include "tst_argvbuilder.moc"
