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
};

class JobControllerTest : public QObject {
    Q_OBJECT
private slots:
    void deleteRunRequiresMatchingSuccessfulPreview();
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

void JobControllerTest::deleteRunRequiresMatchingSuccessfulPreview()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    FakeEngine engine;
    JobController controller(fakeCaps(), &engine, ProfileStore(tmp.path()), SecretStore{},
                             Scheduler{}, false);

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

QTEST_MAIN(JobControllerTest)
#include "tst_jobcontroller.moc"
