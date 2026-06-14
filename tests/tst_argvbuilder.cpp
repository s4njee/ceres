#include <QtTest>

#include <algorithm>

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
    void localTargetHasNoSsh();
    void sshTargetGetsSafeOptions();
    void sshKeyAndPort();
    void daemonTargetHasNoSsh();
    void maxDeleteCap();
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

void ArgvBuilderTest::localTargetHasNoSsh()
{
    SyncJob job;
    job.source = QStringLiteral("/tmp/a/");
    job.destination = QStringLiteral("/tmp/b/");
    QVERIFY(!ArgvBuilder::usesSsh(job));
    QVERIFY(!ArgvBuilder::build(job, modern(), false).contains(QStringLiteral("-e")));
}

void ArgvBuilderTest::sshTargetGetsSafeOptions()
{
    SyncJob job;
    job.source = QStringLiteral("/tmp/a/");
    job.destination = QStringLiteral("user@host:/backup/");
    QVERIFY(ArgvBuilder::usesSsh(job));

    const QStringList args = ArgvBuilder::build(job, modern(), false);
    const int ei = args.indexOf(QStringLiteral("-e"));
    QVERIFY(ei >= 0);
    const QString ssh = args.at(ei + 1);
    QVERIFY(ssh.startsWith(QStringLiteral("ssh ")));
    QVERIFY(ssh.contains(QStringLiteral("BatchMode=yes")));
    QVERIFY(ssh.contains(QStringLiteral("StrictHostKeyChecking=accept-new")));
    QVERIFY(ssh.contains(QStringLiteral("ConnectTimeout=10")));
    // Never weaken host-key checking.
    QVERIFY(!ssh.contains(QStringLiteral("StrictHostKeyChecking=no")));
}

void ArgvBuilderTest::sshKeyAndPort()
{
    SyncJob job;
    job.source = QStringLiteral("server:/data/");
    job.destination = QStringLiteral("/tmp/b/");
    job.sshKeyPath = QStringLiteral("/home/me/.ssh/id_ed25519");
    job.sshPort = 2222;

    const QStringList args = ArgvBuilder::build(job, modern(), false);
    const QString ssh = args.at(args.indexOf(QStringLiteral("-e")) + 1);
    QVERIFY(ssh.contains(QStringLiteral("-i /home/me/.ssh/id_ed25519")));
    QVERIFY(ssh.contains(QStringLiteral("-p 2222")));
}

void ArgvBuilderTest::daemonTargetHasNoSsh()
{
    SyncJob job;
    job.source = QStringLiteral("/tmp/a/");
    job.destination = QStringLiteral("rsync://host/module/");
    QVERIFY(!ArgvBuilder::usesSsh(job));
    QVERIFY(!ArgvBuilder::build(job, modern(), false).contains(QStringLiteral("-e")));
}

void ArgvBuilderTest::maxDeleteCap()
{
    SyncJob job;
    job.source = QStringLiteral("a/");
    job.destination = QStringLiteral("b/");
    job.deleteExtraneous = true;
    job.maxDelete = 50;
    QVERIFY(ArgvBuilder::build(job, modern(), false).contains(QStringLiteral("--max-delete=50")));

    // No cap when the value is 0.
    job.maxDelete = 0;
    const QStringList none = ArgvBuilder::build(job, modern(), false);
    QVERIFY(std::none_of(none.cbegin(), none.cend(),
                         [](const QString &a) { return a.startsWith(QStringLiteral("--max-delete")); }));

    // No cap (or --delete) when deletion is off, even if maxDelete is set.
    job.deleteExtraneous = false;
    job.maxDelete = 50;
    const QStringList off = ArgvBuilder::build(job, modern(), false);
    QVERIFY(!off.contains(QStringLiteral("--delete")));
    QVERIFY(!off.contains(QStringLiteral("--max-delete=50")));
}

QTEST_MAIN(ArgvBuilderTest)
#include "tst_argvbuilder.moc"
