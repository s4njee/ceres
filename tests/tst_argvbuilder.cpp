#include <QtTest>

#include <QDir>

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
    RsyncCapabilities cygwin() const  // a modern Windows (Cygwin) rsync
    {
        RsyncCapabilities c = modern();
        c.pathStyle = RsyncCapabilities::PathStyle::Cygwin;
        return c;
    }
    RsyncCapabilities msys() const  // the selected Windows bundle runtime
    {
        RsyncCapabilities c = modern();
        c.pathStyle = RsyncCapabilities::PathStyle::Msys;
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
    void interactiveSshOmitsBatchMode();
    void sshPasswordModeSwapsAuthOptions();
    void sshKeyAndPort();
    void sshKeyWithSpacesIsQuoted();
    void sshTargetsUseProtectedArgs();
    void daemonTargetHasNoSsh();
    void maxDeleteCap();
    void expandsLocalTilde();
    void convertsWindowsLocalPaths();
    void windowsBuildConvertsLocalEndpoints();
    void windowsConvertsSshKeyPath();
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

void ArgvBuilderTest::interactiveSshOmitsBatchMode()
{
    SyncJob job;
    job.source = QStringLiteral("/tmp/a/");
    job.destination = QStringLiteral("user@host:/backup/");

    ArgvBuilder::BuildOptions options;
    options.allowInteractiveSsh = true;
    const QStringList args = ArgvBuilder::build(job, modern(), options);
    const QString ssh = args.at(args.indexOf(QStringLiteral("-e")) + 1);

    QVERIFY(!ssh.contains(QStringLiteral("BatchMode=yes")));
    QVERIFY(ssh.contains(QStringLiteral("StrictHostKeyChecking=accept-new")));
    QVERIFY(ssh.contains(QStringLiteral("ConnectTimeout=10")));
}

void ArgvBuilderTest::sshPasswordModeSwapsAuthOptions()
{
    SyncJob job;
    job.source = QStringLiteral("/tmp/a/");
    job.destination = QStringLiteral("user@host:/backup/");
    job.sshPassword = QStringLiteral("s3cr3t");

    const QStringList args = ArgvBuilder::build(job, modern(), false);
    const QString ssh = args.at(args.indexOf(QStringLiteral("-e")) + 1);

    // Password mode drops BatchMode (which would disable password auth) and steers
    // ssh to password/keyboard-interactive with a single prompt.
    QVERIFY(!ssh.contains(QStringLiteral("BatchMode")));
    QVERIFY(ssh.contains(QStringLiteral("PreferredAuthentications=password,keyboard-interactive")));
    QVERIFY(ssh.contains(QStringLiteral("NumberOfPasswordPrompts=1")));
    QVERIFY(ssh.contains(QStringLiteral("StrictHostKeyChecking=accept-new")));

    // The password is fed via SSH_ASKPASS, never on the command line.
    QVERIFY(!ssh.contains(QStringLiteral("s3cr3t")));
    for (const QString &a : args)
        QVERIFY(!a.contains(QStringLiteral("s3cr3t")));
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

void ArgvBuilderTest::sshKeyWithSpacesIsQuoted()
{
    SyncJob job;
    job.source = QStringLiteral("server:/data/");
    job.destination = QStringLiteral("/tmp/b/");
    job.sshKeyPath = QStringLiteral("/home/me/ssh keys/id_ed25519");

    const QStringList args = ArgvBuilder::build(job, modern(), false);
    const QString ssh = args.at(args.indexOf(QStringLiteral("-e")) + 1);
    QVERIFY(ssh.contains(QStringLiteral("-i '/home/me/ssh keys/id_ed25519'")));
}

void ArgvBuilderTest::sshTargetsUseProtectedArgs()
{
    SyncJob job;
    job.source = QStringLiteral("/tmp/source with spaces/");
    job.destination = QStringLiteral("server:/backup with spaces/");

    const QStringList modernArgs = ArgvBuilder::build(job, modern(), false);
    QVERIFY(modernArgs.contains(QStringLiteral("--protect-args")));

    const QStringList openArgs = ArgvBuilder::build(job, openrsync(), false);
    QVERIFY(!openArgs.contains(QStringLiteral("--protect-args")));
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

void ArgvBuilderTest::expandsLocalTilde()
{
    const QString home = QDir::homePath();

    // Leading ~/ on a local path is expanded.
    SyncJob j;
    j.source = QStringLiteral("~/src/");
    j.destination = QStringLiteral("/tmp/d/");
    const QStringList a = ArgvBuilder::build(j, modern(), false);
    QCOMPARE(a.at(a.size() - 2), home + QStringLiteral("/src/"));
    QCOMPARE(a.last(), QStringLiteral("/tmp/d/"));

    // Bare ~ becomes the home dir.
    SyncJob bare;
    bare.source = QStringLiteral("~");
    bare.destination = QStringLiteral("/tmp/d/");
    const QStringList b = ArgvBuilder::build(bare, modern(), false);
    QCOMPARE(b.at(b.size() - 2), home);

    // A remote tilde is left for the remote shell.
    SyncJob remote;
    remote.source = QStringLiteral("/tmp/s/");
    remote.destination = QStringLiteral("host:~/backup/");
    QCOMPARE(ArgvBuilder::build(remote, modern(), false).last(), QStringLiteral("host:~/backup/"));
}

void ArgvBuilderTest::convertsWindowsLocalPaths()
{
    using PS = RsyncCapabilities::PathStyle;

    // Native passes through untouched (mac/Linux paths are never rewritten).
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("/tmp/x"), PS::Native),
             QStringLiteral("/tmp/x"));
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("C:\\Users\\me"), PS::Native),
             QStringLiteral("C:\\Users\\me"));

    // Cygwin: drive maps to /cygdrive/<letter>/, both slash styles, drive lowercased.
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("C:\\Users\\me"), PS::Cygwin),
             QStringLiteral("/cygdrive/c/Users/me"));
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("C:/Users/me"), PS::Cygwin),
             QStringLiteral("/cygdrive/c/Users/me"));
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("D:\\data"), PS::Cygwin),
             QStringLiteral("/cygdrive/d/data"));
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("C:"), PS::Cygwin),
             QStringLiteral("/cygdrive/c"));

    // MSYS2 standalone builds default to the same /cygdrive prefix as Cygwin
    // (the "/c" form needs a full install's etc/fstab, which a bundle lacks —
    // verified: a fstab-less MSYS2 rsync fails on "/c/..." paths).
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("C:\\Users\\me"), PS::Msys),
             QStringLiteral("/cygdrive/c/Users/me"));
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("D:\\data"), PS::Msys),
             QStringLiteral("/cygdrive/d/data"));

    // Non-drive paths only get their slashes normalised.
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("/already/posix"), PS::Cygwin),
             QStringLiteral("/already/posix"));
    QCOMPARE(ArgvBuilder::toRsyncLocalPath(QStringLiteral("\\\\server\\share"), PS::Cygwin),
             QStringLiteral("//server/share"));
}

void ArgvBuilderTest::windowsBuildConvertsLocalEndpoints()
{
    SyncJob job;
    job.source = QStringLiteral("C:\\src");
    job.destination = QStringLiteral("user@host:/backup");  // remote: must stay raw

    const QStringList args = ArgvBuilder::build(job, cygwin(), false);
    QCOMPARE(args.at(args.size() - 2), QStringLiteral("/cygdrive/c/src"));
    QCOMPARE(args.last(), QStringLiteral("user@host:/backup"));

    const QStringList msysArgs = ArgvBuilder::build(job, msys(), false);
    QCOMPARE(msysArgs.at(msysArgs.size() - 2), QStringLiteral("/cygdrive/c/src"));
    QCOMPARE(msysArgs.last(), QStringLiteral("user@host:/backup"));
}

void ArgvBuilderTest::windowsConvertsSshKeyPath()
{
    SyncJob job;
    job.source = QStringLiteral("C:\\src");
    job.destination = QStringLiteral("user@host:/backup");
    job.sshKeyPath = QStringLiteral("C:\\Users\\me\\.ssh\\id_ed25519");

    const QStringList args = ArgvBuilder::build(job, cygwin(), false);
    const QString ssh = args.at(args.indexOf(QStringLiteral("-e")) + 1);
    QVERIFY(ssh.contains(QStringLiteral("-i /cygdrive/c/Users/me/.ssh/id_ed25519")));

    const QStringList msysArgs = ArgvBuilder::build(job, msys(), false);
    const QString msysSsh = msysArgs.at(msysArgs.indexOf(QStringLiteral("-e")) + 1);
    QVERIFY(msysSsh.contains(QStringLiteral("-i /cygdrive/c/Users/me/.ssh/id_ed25519")));

#ifdef Q_OS_WIN
    // The bundled ssh finds ~/.ssh via getpwuid (a nonexistent /home/<user>), so
    // known_hosts must be pinned to the real profile's .ssh in /cygdrive form.
    QVERIFY(ssh.contains(QStringLiteral("UserKnownHostsFile=/cygdrive/")));
    QVERIFY(ssh.contains(QStringLiteral("/.ssh/known_hosts")));
#endif
}

QTEST_MAIN(ArgvBuilderTest)
#include "tst_argvbuilder.moc"
