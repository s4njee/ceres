#include <QtTest>

#include <QList>

#include "app/TransferManager.h"
#include "engine/SyncEngine.h"
#include "models/TransfersModel.h"

// Test double mirroring tst_jobcontroller.cpp's FakeEngine: records start()
// calls and lets the test drive lifecycle via finish()/sendProgress().
class FakeEngine : public SyncEngine {
    Q_OBJECT
public:
    int starts = 0;
    int cancels = 0;
    bool running = false;
    SyncJob lastJob;

    void start(const SyncJob &job, bool dryRun) override
    {
        ++starts;
        lastJob = job;
        Q_UNUSED(dryRun);
        running = true;
        emit started();
    }

    void cancel() override
    {
        ++cancels;
        running = false;
    }

    bool isRunning() const override { return running; }

    void finish(int code = 0, bool crashed = false)
    {
        running = false;
        emit finished(code, crashed);
    }

    void sendProgress(int percent, const QString &rate)
    {
        ProgressInfo info;
        info.percent = percent;
        info.rate = rate;
        emit progress(info);
    }

    void failStart(const QString &reason) { emit failedToStart(reason); }
};

// Hands out FakeEngine instances and keeps references so the test can drive
// them. The manager takes ownership (setParent), so we don't delete them here.
class FakeEngineFactory {
public:
    QList<FakeEngine *> created;

    SyncEngine *operator()()
    {
        auto *e = new FakeEngine;
        created.append(e);
        return e;
    }
};

class TransferManagerTest : public QObject {
    Q_OBJECT
private slots:
    void capRespected();
    void queueDrains();
    void failureMarksFailed();
    void cancelQueuedAndActive();
    void enqueuedSignalFiresPerEnqueue();
};

static SyncJob jobN(int n)
{
    SyncJob j;
    j.source = QStringLiteral("/tmp/src%1/").arg(n);
    j.destination = QStringLiteral("/tmp/dst%1/").arg(n);
    return j;
}

// Count rows in a given status by reading the model the way QML would.
static int countStatus(TransfersModel *m, TransfersModel::Status status)
{
    int n = 0;
    for (int i = 0; i < m->rowCount(); ++i) {
        const QModelIndex mi = m->index(i);
        if (m->data(mi, TransfersModel::StatusRole).toInt() == static_cast<int>(status))
            ++n;
    }
    return n;
}

void TransferManagerTest::capRespected()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);

    for (int i = 0; i < 4; ++i)
        mgr.enqueue(jobN(i), QStringLiteral("up"), QStringLiteral("t%1").arg(i));

    // Exactly two engines started; two rows Active, two still Queued.
    QCOMPARE(factory.created.size(), 2);
    QCOMPARE(mgr.activeCount(), 2);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Active), 2);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Queued), 2);

    // Finishing one admits a third from the queue.
    factory.created.at(0)->finish(0);
    QCOMPARE(factory.created.size(), 3);
    QCOMPARE(mgr.activeCount(), 2);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Done), 1);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Active), 2);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Queued), 1);
}

void TransferManagerTest::queueDrains()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);

    for (int i = 0; i < 4; ++i)
        mgr.enqueue(jobN(i), QStringLiteral("down"), QStringLiteral("t%1").arg(i));

    // Finish each engine as it appears; the queue should fully drain to 4 engines.
    int finished = 0;
    while (finished < 4) {
        QVERIFY(finished < factory.created.size());
        factory.created.at(finished)->finish(0);
        ++finished;
    }

    QCOMPARE(factory.created.size(), 4);
    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Done), 4);
    QCOMPARE(mgr.model()->activeCount(), 0);
}

void TransferManagerTest::failureMarksFailed()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);

    const QString id0 = mgr.enqueue(jobN(0), QStringLiteral("up"), QStringLiteral("t0"));
    mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("t1"));

    factory.created.at(0)->finish(23);  // non-zero exit
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Failed), 1);

    // Verify the failed row is the first transfer's row.
    const QModelIndex mi = mgr.model()->index(0);
    QCOMPARE(mgr.model()->data(mi, TransfersModel::IdRole).toString(), id0);
    QCOMPARE(mgr.model()->data(mi, TransfersModel::StatusRole).toInt(),
             static_cast<int>(TransfersModel::Failed));
}

void TransferManagerTest::cancelQueuedAndActive()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(1);

    const QString idActive = mgr.enqueue(jobN(0), QStringLiteral("up"), QStringLiteral("t0"));
    const QString idQueued = mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("t1"));

    QCOMPARE(factory.created.size(), 1);  // only the first started under cap=1

    // Cancelling the still-queued transfer marks it Cancelled and never starts it.
    mgr.cancel(idQueued);
    QCOMPARE(factory.created.size(), 1);  // no new engine
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Cancelled), 1);

    // Cancelling the active transfer calls cancel() on its engine; the engine
    // then reports an interrupted run (crashed=true), which becomes Cancelled.
    mgr.cancel(idActive);
    QCOMPARE(factory.created.at(0)->cancels, 1);
    factory.created.at(0)->finish(0, /*crashed=*/true);

    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Cancelled), 2);
    Q_UNUSED(idActive);
}

void TransferManagerTest::enqueuedSignalFiresPerEnqueue()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);

    QSignalSpy spy(&mgr, &TransferManager::enqueued);

    mgr.enqueue(jobN(0), QStringLiteral("up"), QStringLiteral("t0"));
    QCOMPARE(spy.count(), 1);

    mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("t1"));
    mgr.enqueue(jobN(2), QStringLiteral("up"), QStringLiteral("t2"));
    QCOMPARE(spy.count(), 3);
}

QTEST_MAIN(TransferManagerTest)
#include "tst_transfermanager.moc"
