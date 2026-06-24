#include <QtTest>

#include <QTemporaryDir>

#include "app/JobController.h"
#include "engine/SyncEngine.h"

class FakeEngine : public SyncEngine {
    Q_OBJECT
public:
    int starts = 0;
    bool lastDryRun = false;
    bool running = false;
    SyncJob lastJob;

    void start(const SyncJob &job, bool dryRun) override
    {
        ++starts;
        lastJob = job;
        lastDryRun = dryRun;
        running = true;
        emit started();
    }

    void cancel() override { running = false; }
    bool isRunning() const override { return running; }

    void finish(int code = 0, bool crashed = false)
    {
        running = false;
        emit finished(code, crashed);
    }

    void sendProgress(int percent, const QString &rate, qint64 bytes = 0)
    {
        ProgressInfo info;
        info.percent = percent;
        info.rate = rate;
        info.bytes = bytes;
        emit progress(info);
    }

    void sendStderr(const QString &line) { emit errorOutput(line); }
};

class JobControllerTest : public QObject {
    Q_OBJECT
private slots:
    void deleteRunRequiresMatchingSuccessfulPreview();
    void progressSpeedIsExposedAndReset();
    void realSyncProgressSetsTransferringStatus();
    void sshKeyFailurePromptsThenRetriesWithPassword();
    void savedSshHostsFollowSavedJobs();
    void explicitSshHostCanBeSavedWithoutJob();
};

static RsyncCapabilities fakeCaps()
{
    RsyncCapabilities caps;
    caps.found = true;
    caps.path = QStringLiteral("/tmp/rsync");
    caps.major = 3;
    caps.minor = 2;
    caps.versionString = QStringLiteral("3.2");
    return caps;
}

static QVariantMap deleteJob()
{
    return {
        {QStringLiteral("name"), QStringLiteral("Danger")},
        {QStringLiteral("source"), QStringLiteral("/tmp/source/")},
        {QStringLiteral("destination"), QStringLiteral("/tmp/dest/")},
        {QStringLiteral("archive"), true},
        {QStringLiteral("deleteExtras"), true},
        {QStringLiteral("maxDelete"), 50},
    };
}

static SshHostStore tempHostStore(const QTemporaryDir &tmp)
{
    return SshHostStore(tmp.path() + QStringLiteral("/ssh-hosts.json"));
}

void JobControllerTest::deleteRunRequiresMatchingSuccessfulPreview()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    FakeEngine engine;
    JobController controller(fakeCaps(), &engine, ProfileStore(tmp.path()), SecretStore{},
                             Scheduler{}, tempHostStore(tmp), false);

    QVariantMap job = deleteJob();
    controller.run(job);
    QCOMPARE(engine.starts, 0);
    QVERIFY(controller.status().contains(QStringLiteral("Preview this exact delete sync")));

    controller.preview(job);
    QCOMPARE(engine.starts, 1);
    QVERIFY(engine.lastDryRun);
    engine.finish();
    QVERIFY(controller.status().contains(QStringLiteral("Preview complete")));

    QVariantMap edited = job;
    edited[QStringLiteral("destination")] = QStringLiteral("/tmp/other/");
    controller.run(edited);
    QCOMPARE(engine.starts, 1);
    QVERIFY(controller.status().contains(QStringLiteral("Preview this exact delete sync")));

    controller.run(job);
    QCOMPARE(engine.starts, 2);
    QVERIFY(!engine.lastDryRun);
    engine.finish();

    controller.run(job);
    QCOMPARE(engine.starts, 2);
    QVERIFY(controller.status().contains(QStringLiteral("Preview this exact delete sync")));
}

void JobControllerTest::progressSpeedIsExposedAndReset()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    FakeEngine engine;
    JobController controller(fakeCaps(), &engine, ProfileStore(tmp.path()), SecretStore{},
                             Scheduler{}, tempHostStore(tmp), false);

    QVariantMap job = {
        {QStringLiteral("source"), QStringLiteral("/tmp/source/")},
        {QStringLiteral("destination"), QStringLiteral("/tmp/dest/")},
    };

    QVERIFY(controller.speed().isEmpty());
    QVERIFY(controller.bytesProgress().isEmpty());
    controller.preview(job);
    QCOMPARE(controller.percent(), 0);
    QVERIFY(controller.speed().isEmpty());
    QVERIFY(controller.bytesProgress().isEmpty());

    engine.sendProgress(42, QStringLiteral("512.00kB/s"), 524288);
    QCOMPARE(controller.percent(), 42);
    QCOMPARE(controller.speed(), QStringLiteral("512.00kB/s"));
    QCOMPARE(controller.bytesProgress(), QStringLiteral("512 kB / 1.2 MB"));

    engine.finish();
    controller.preview(job);
    QCOMPARE(controller.percent(), 0);
    QVERIFY(controller.speed().isEmpty());
    QVERIFY(controller.bytesProgress().isEmpty());
}

void JobControllerTest::realSyncProgressSetsTransferringStatus()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    FakeEngine engine;
    JobController controller(fakeCaps(), &engine, ProfileStore(tmp.path()), SecretStore{},
                             Scheduler{}, tempHostStore(tmp), false);

    QVariantMap job = {
        {QStringLiteral("source"), QStringLiteral("/tmp/source/")},
        {QStringLiteral("destination"), QStringLiteral("/tmp/dest/")},
    };

    controller.run(job);
    QCOMPARE(controller.status(), QStringLiteral("Scanning…"));

    engine.sendProgress(7, QStringLiteral("1.25MB/s"));
    QCOMPARE(controller.status(), QStringLiteral("Transferring…"));
}

void JobControllerTest::sshKeyFailurePromptsThenRetriesWithPassword()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    FakeEngine engine;
    JobController controller(fakeCaps(), &engine, ProfileStore(tmp.path()), SecretStore{},
                             Scheduler{}, tempHostStore(tmp), false);

    QSignalSpy authSpy(&controller, &JobController::sshAuthRequired);

    QVariantMap job = {
        {QStringLiteral("source"), QStringLiteral("/tmp/source/")},
        {QStringLiteral("destination"), QStringLiteral("host:/backup/")},
    };

    controller.run(job);
    QCOMPARE(engine.starts, 1);
    QVERIFY(engine.lastJob.sshPassword.isEmpty());  // first attempt uses the key

    // ssh prints a permission-denied line, then rsync exits with the SSH error code.
    engine.sendStderr(QStringLiteral("Permission denied (publickey,password)."));
    engine.finish(255);

    QCOMPARE(authSpy.count(), 1);
    QCOMPARE(authSpy.at(0).at(0).toString(), QStringLiteral("host"));  // host parsed out
    QCOMPARE(authSpy.at(0).at(1).toString(), QString());               // no user in endpoint

    // The modal supplies credentials; the run repeats with the password and user@host.
    controller.retryWithPassword(job, QStringLiteral("bob"), QStringLiteral("hunter2"), false);
    QCOMPARE(engine.starts, 2);
    QCOMPARE(engine.lastJob.sshPassword, QStringLiteral("hunter2"));
    QCOMPARE(engine.lastJob.destination, QStringLiteral("bob@host:/backup/"));

    // A password attempt that also fails must not loop back into another prompt.
    engine.sendStderr(QStringLiteral("Permission denied (publickey,password)."));
    engine.finish(255);
    QCOMPARE(authSpy.count(), 1);
}

void JobControllerTest::savedSshHostsFollowSavedJobs()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    FakeEngine engine;
    JobController controller(fakeCaps(), &engine, ProfileStore(tmp.path()), SecretStore{},
                             Scheduler{}, tempHostStore(tmp), false);

    QVariantMap local = {
        {QStringLiteral("name"), QStringLiteral("Local")},
        {QStringLiteral("source"), QStringLiteral("/tmp/source/")},
        {QStringLiteral("destination"), QStringLiteral("/tmp/dest/")},
    };
    controller.saveJob(local);
    QCOMPARE(controller.sshHosts()->rowCount(), 0);

    QVariantMap ssh = {
        {QStringLiteral("name"), QStringLiteral("Remote")},
        {QStringLiteral("source"), QStringLiteral("/tmp/source/")},
        {QStringLiteral("destination"), QStringLiteral("alice@example.com:/backup/")},
    };
    controller.newJob();
    controller.saveJob(ssh);

    QCOMPARE(controller.sshHosts()->rowCount(), 1);
    const QModelIndex hostIndex = controller.sshHosts()->index(0);
    QCOMPARE(controller.sshHosts()->data(hostIndex, SshHostListModel::TargetRole).toString(),
             QStringLiteral("alice@example.com"));
    QCOMPARE(controller.sshHosts()->data(hostIndex, SshHostListModel::HostRole).toString(),
             QStringLiteral("example.com"));
    QCOMPARE(controller.sshHosts()->data(hostIndex, SshHostListModel::UserRole).toString(),
             QStringLiteral("alice"));

    const QString sshJobId =
        controller.sshHosts()->data(hostIndex, SshHostListModel::FirstJobIdRole).toString();
    controller.deleteJob(sshJobId);
    QCOMPARE(controller.sshHosts()->rowCount(), 0);
}

void JobControllerTest::explicitSshHostCanBeSavedWithoutJob()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    FakeEngine engine;
    JobController controller(fakeCaps(), &engine, ProfileStore(tmp.path()), SecretStore{},
                             Scheduler{}, tempHostStore(tmp), false);

    QVariantMap ssh = {
        {QStringLiteral("name"), QStringLiteral("Unsaved Remote")},
        {QStringLiteral("source"), QStringLiteral("/tmp/source/")},
        {QStringLiteral("destination"), QStringLiteral("alice@example.com:/backup/")},
        {QStringLiteral("sshPort"), 2222},
    };

    QCOMPARE(controller.sshTargetForJob(ssh), QStringLiteral("alice@example.com"));
    QVERIFY(!controller.isSshHostSaved(QStringLiteral("alice@example.com")));

    controller.saveSshHostForJob(ssh);
    QVERIFY(controller.isSshHostSaved(QStringLiteral("alice@example.com")));
    QCOMPARE(controller.sshHosts()->rowCount(), 1);

    const QModelIndex hostIndex = controller.sshHosts()->index(0);
    QCOMPARE(controller.sshHosts()->data(hostIndex, SshHostListModel::TargetRole).toString(),
             QStringLiteral("alice@example.com"));
    QCOMPARE(controller.sshHosts()->data(hostIndex, SshHostListModel::SummaryRole).toString(),
             QStringLiteral("saved host"));
    QCOMPARE(controller.sshHosts()->data(hostIndex, SshHostListModel::JobCountRole).toInt(), 0);
}

QTEST_MAIN(JobControllerTest)
#include "tst_jobcontroller.moc"
